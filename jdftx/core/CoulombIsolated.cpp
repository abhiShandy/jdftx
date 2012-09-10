/*-------------------------------------------------------------------
Copyright 2012 Ravishankar Sundararaman

This file is part of JDFTx.

JDFTx is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

JDFTx is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with JDFTx.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------*/

#include <core/CoulombIsolated.h>
#include <core/Coulomb_internal.h>
#include <core/Operators.h>
#include <core/Util.h>
#include <core/Thread.h>
#include <core/LoopMacros.h>
#include <core/BlasExtra.h>

//! Analog of ewald sum for isolated systems
//! (no Ewald trick required, just for consistent naming)
struct EwaldIsolated
{	const GridInfo& gInfo;
	const WignerSeitz& ws;
	bool wsTruncated; //true => Wigner-Seitz truncation, false => spherical
	double criticalDist; //borderWidth for Wigner-Seitz, Rc for spherical
	
	EwaldIsolated(const GridInfo& gInfo, const WignerSeitz& ws, bool wsTruncated, double criticalDist)
	: gInfo(gInfo), ws(ws), wsTruncated(wsTruncated), criticalDist(criticalDist)
	{
	}
	
	double energyAndGrad(std::vector<Coulomb::PointCharge>& pointCharges) const
	{	double E = 0.;
		//Shift all points into a Wigner-Seitz cell centered on one of the atoms; choice of this atom
		//is irrelevant if every atom lies in the WS cell of the other with a consistent translation:
		vector3<> pos0 = pointCharges[0].pos;
		for(Coulomb::PointCharge& pc: pointCharges)
			pc.pos = pos0 + ws.restrict(pc.pos - pos0);
		//Loop over all pairs of pointcharges:
		for(unsigned i=0; i<pointCharges.size(); i++)
		{	Coulomb::PointCharge& pc1 = pointCharges[i];
			for(unsigned j=0; j<i; j++)
			{	Coulomb::PointCharge& pc2 = pointCharges[j];
				vector3<> x = pc1.pos - pc2.pos; //lattice coords
				double rSq = gInfo.RTR.metric_length_squared(x), r = sqrt(rSq);
				if(wsTruncated)
				{	if(ws.boundaryDistance(x) <= criticalDist)
						die("Separation between atoms %d and %d lies in the truncation border.\n", i, j);
				}
				else
				{	if(r >= criticalDist)
						die("Atoms %d and %d are separated by r = %lg >= Rc = %lg bohrs.\n", i, j, r, criticalDist);
				}
				double dE = (pc1.Z * pc2.Z) / r;
				vector3<> dF = (gInfo.RTR * x) * (dE/rSq);
				E += dE;
				pc1.force += dF;
				pc2.force -= dF;
			}
		}
		return E;
	}
};

//----------------- class CoulombIsolated ---------------------

//Set the fourier transform of the simplex theta function
inline void simplexTheta_thread(int iStart, int iStop, const vector3<int> S, const matrix3<> GT, double Vcell,
	fftw_complex* theta, const std::vector<Simplex<3>>* simplexArr, double sigma)
{
	double volPrefac = 1./Vcell;
	THREAD_halfGspaceLoop
	(	vector3<> G = GT * iG; //reciprocal lattice vector in cartesian coords
		Simplex<3>::Point Gpoint({{ G[0], G[1], G[2] }}); //convert to Simplex<3>::Point
		double curTheta = 0.;
		for(const Simplex<3>& simplex: *simplexArr)
			curTheta += simplex.getTilde(Gpoint);
		theta[i][0] = volPrefac * curTheta * exp(-0.5*G.length_squared()*sigma*sigma);
		theta[i][1] = 0.; //no imaginary part by inversion symmetry
	)
}

//Multiply by the erf-smoothened coulomb potential in real space (twice the Wigner-Seitz cell)
inline void multErfCoulomb_thread(int iStart, int iStop, const vector3<int> S, const matrix3<> RTR,
	double* data, WignerSeitz* ws, double sigma, double dV)
{
	matrix3<> invDiagS = inv(Diag(vector3<>(S)));
	vector3<int> pitch;
	pitch[2] = 1;
	pitch[1] = pitch[2] * 2*(1+S[2]/2);
	pitch[0] = pitch[1] * S[1];
	double a = sqrt(0.5)/sigma;
	THREAD_rLoop
	(	vector3<> x = ws->restrict(invDiagS * iv); //position in lattice coordinates within WignerSeitz cell
		double ar = a * sqrt(RTR.metric_length_squared(x)); //distance of minmal periodic image from origin
		data[dot(pitch,iv)] *= (dV*a) * erf_by_x(ar); //include normalization factor for subsequent Fourier transform
	)
}

//Restrict from supercell to original cell, and add contribution from erfc/r
inline void downSample_thread(int iStart, int iStop, const vector3<int>& S, const matrix3<>& GGT,
	const fftw_complex* in, const vector3<int>& Ssup, double* out, double sigma)
{
	vector3<int> pitchSup;
	pitchSup[2] = 1;
	pitchSup[1] = pitchSup[2] * (1+Ssup[2]/2);
	pitchSup[0] = pitchSup[1] * Ssup[1];
	double hlfSigmaSq = 0.5*sigma*sigma;
	THREAD_halfGspaceLoop
	(	//Find corresponding point in supercell fourier transform:
		int iSup = 0;
		for(int k=0; k<3; k++)
		{	int ik = 2 * iG[k];
			if(ik<0) ik += Ssup[k];
			iSup += pitchSup[k] * ik;
		}
		//Store in smaller cell, along with short-ranged part:
		double Gsq = GGT.metric_length_squared(iG);
		out[i] = in[iSup][0] + (4*M_PI) * (Gsq ? (1.-exp(-hlfSigmaSq*Gsq))/Gsq : hlfSigmaSq);
	)
}



CoulombIsolated::CoulombIsolated(const GridInfo& gInfo, const CoulombTruncationParams& params)
: Coulomb(gInfo, params), ws(gInfo.R), Vc(gInfo)
{
	//Read precomputed kernel from file if supplied
	if(params.filename.length())
	{	FILE* fp = fopen(params.filename.c_str(), "rb");
		if(fp)
		{	matrix3<> R; vector3<int> S; double bw;
			fread(&R, sizeof(matrix3<>), 1, fp);
			fread(&S, sizeof(vector3<int>), 1, fp);
			fread(&bw, sizeof(double), 1, fp);
			if(R != gInfo.R)
				logPrintf("Precomputed coulomb kernel file '%s' has different lattice vectors (recomputing it now)\n", params.filename.c_str());
			else if(!(S == gInfo.S))
				logPrintf("Precomputed coulomb kernel file '%s' has different sample count (recomputing it now)\n", params.filename.c_str());
			else if(bw != params.borderWidth)
				logPrintf("Precomputed coulomb kernel file '%s' has different border width (recomputing it now)\n", params.filename.c_str());
			else if(fread(Vc.data, sizeof(double), gInfo.nG, fp) != unsigned(gInfo.nG))
				logPrintf("Error reading precomputed coulomb kernel from '%s' (computing it now)\n", params.filename.c_str());
			else
			{	logPrintf("Successfully read precomputed coulomb kernel from '%s'\n", params.filename.c_str());
				Vc.set();
				return;
			}
		}
		else logPrintf("Could not open precomputed coulomb kernel file '%s' (computing it now)\n", params.filename.c_str());
	}
	
	//Select gauss-smoothing parameter:
	double maxBorderWidth = 0.5 * ws.inRadius();
	if(params.borderWidth > maxBorderWidth)
		die("Border width %lg bohrs must be less than half the Wigner-Seitz cell in-radius = %lg bohrs.\n",
			params.borderWidth, maxBorderWidth);
	double sigma = 0.1 * params.borderWidth;
	logPrintf("Selecting gaussian width %lg bohrs (for border width %lg bohrs).\n", sigma, params.borderWidth);
	
	//Set up supercell:
	logPrintf("Setting up 2x2x2 supercell: ");
	matrix3<> Rsup = 2.*gInfo.R;
	matrix3<> GTsup = (2*M_PI)*~inv(Rsup);
	double Gnyq = 10./sigma; //lower bound on Nyquist frequency
	vector3<int> Ssup;
	for(int k=0; k<3; k++)
	{	Ssup[k] = 2*int(ceil(Gnyq * Rsup.column(k).length() / (2*M_PI)));
		if(Ssup[k] < 2*gInfo.S[k]) Ssup[k] = 2*gInfo.S[k]; //ensure sufficient resolution for final result
		while(!fftSuitable(Ssup[k])) Ssup[k]+=2; //pick the next even number suitable for FFT
	}
	logPrintf("sample count = "); Ssup.print(globalLog, " %d ");
	int nGsup = Ssup[0] * Ssup[1] * (1+Ssup[2]/2); //number of symmetry reduced reciprocal lattice vectors
	int nrSup = Ssup[0] * Ssup[1] * Ssup[2]; //number of real space points
	double Vcell = fabs(det(Rsup));
	
	//Plan Fourier transforms:
	fftw_complex* fftArr = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*nGsup);
	if(!fftArr)
		die("Insufficient memory (need %.1fGB). Hint: try increasing border width.\n", 1.6e-8*nGsup);
	logPrintf("Planning supercell fourier transforms ... "); logFlush();
	fftw_plan_with_nthreads(nProcsAvailable);
	fftw_plan fftPlanC2R = fftw_plan_dft_c2r_3d(Ssup[0], Ssup[1], Ssup[2], fftArr, (double*)fftArr, FFTW_ESTIMATE);
	fftw_plan fftPlanR2C = fftw_plan_dft_r2c_3d(Ssup[0], Ssup[1], Ssup[2], (double*)fftArr, fftArr, FFTW_ESTIMATE);
	logPrintf("Done.\n");
	
	//Initialize smoothed theta function:
	logPrintf("Computing truncation shape function ... "); logFlush();
	std::vector<Simplex<3>> sArr = ws.getSimplices3D();
	threadLaunch(simplexTheta_thread, nGsup, Ssup, GTsup, Vcell, fftArr, &sArr, sigma);
	fftw_execute(fftPlanC2R);
	logPrintf("Done.\n");
	
	//Multiply by long-ranged erf/r:
	logPrintf("Applying truncation to Coulomb kernel ... "); logFlush();
	threadLaunch(multErfCoulomb_thread, nrSup, Ssup, (~Rsup)*Rsup, (double*)fftArr, &ws, sigma, Vcell/nrSup);
	fftw_execute(fftPlanR2C);
	logPrintf("Done.\n");
	
	//Restrict to original sample count and add short-ranged erfc/r:
	threadLaunch(downSample_thread, gInfo.nG, gInfo.S, gInfo.GGT, fftArr, Ssup, Vc.data, sigma);
	fftw_free(fftArr);
	Vc.set();
	
	//Save kernel if requested:
	if(params.filename.length())
	{	logPrintf("Saving isolated coulomb kernel to '%s' ... ", params.filename.c_str()); logFlush();
		FILE* fp = fopen(params.filename.c_str(), "wb");
		if(!fp) die("could not open file for writing.\n");
		fwrite(&gInfo.R, sizeof(matrix3<>), 1, fp);
		fwrite(&gInfo.S, sizeof(vector3<int>), 1, fp);
		fwrite(&params.borderWidth, sizeof(double), 1, fp);
		fwrite(Vc.data, sizeof(double), gInfo.nG, fp);
		fclose(fp);
		logPrintf("Done.\n");
	}
}

DataGptr CoulombIsolated::operator()(DataGptr&& in) const
{	return Vc * in;
}

double CoulombIsolated::energyAndGrad(std::vector<Coulomb::PointCharge>& pointCharges) const
{	return EwaldIsolated(gInfo, ws, true, params.borderWidth).energyAndGrad(pointCharges);
}


//----------------- class CoulombSpherical ---------------------

CoulombSpherical::CoulombSpherical(const GridInfo& gInfo, const CoulombTruncationParams& params)
: Coulomb(gInfo, params), ws(gInfo.R), Rc(params.Rc)
{	double RcMax = ws.inRadius();
	if(Rc > RcMax)
		die("Spherical truncation radius %lg exceeds Wigner-Seitz cell in-radius of %lg bohrs.\n", Rc, RcMax);
	if(!Rc) Rc = RcMax;
	logPrintf("Initialized spherical truncation of radius %lg bohrs\n", Rc);
}

DataGptr CoulombSpherical::operator()(DataGptr&& in) const
{	callPref(coulombAnalytic)(gInfo.S, gInfo.GGT, CoulombSpherical_calc(Rc), in->dataPref(false));
	return in;
}

double CoulombSpherical::energyAndGrad(std::vector<Coulomb::PointCharge>& pointCharges) const
{	return EwaldIsolated(gInfo, ws, false, Rc).energyAndGrad(pointCharges);
}