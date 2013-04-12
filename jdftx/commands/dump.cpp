/*-------------------------------------------------------------------
Copyright 2011 Ravishankar Sundararaman

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

#include <commands/command.h>
#include <electronic/Everything.h>
#include <electronic/Polarizability.h>


EnumStringMap<DumpFrequency> freqMap
(	DumpFreq_End, "End",
	DumpFreq_Lattice, "Lattice",
	DumpFreq_Ionic, "Ionic",
	DumpFreq_Gummel, "Gummel",
	DumpFreq_Fluid, "Fluid",
	DumpFreq_Electronic, "Electronic"
);
EnumStringMap<DumpFrequency> freqDescMap
(	DumpFreq_End, "Dump specified vars at the end of the calculation",
	DumpFreq_Lattice, "Dump specified vars every (few) lattice minimization step(s)",
	DumpFreq_Ionic, "Dump specified vars every (few) ionic step(s)",
	DumpFreq_Gummel, "Dump specified vars every (few) fluid+electron minimize of the gummel loop",
	DumpFreq_Fluid, "Dump specified vars every (few) fluid step(s)",
	DumpFreq_Electronic, "Dump specified vars every (few) electronic step(s)"
);


EnumStringMap<DumpVariable> varMap
(	DumpAll, "All",
	DumpNone, "None",
	DumpState, "State",
	DumpIonicPositions, "IonicPositions",
	DumpForces, "Forces",
	DumpLattice, "Lattice",
	DumpIonicDensity, "IonicDensity",
	DumpElecDensity, "ElecDensity",
	DumpCoreDensity, "CoreDensity",
	DumpKEdensity, "KEdensity",
	DumpFluidDensity, "FluidDensity",
	DumpDvac, "Dvac",
	DumpDfluid, "Dfluid",
	DumpDtot, "Dtot",
	DumpVcavity, "Vcavity",
	DumpVfluidTot, "VfluidTot",
	DumpVlocps, "Vlocps",
	DumpVscloc, "Vscloc",
	DumpHsubEvecs, "HsubEvecs",
	DumpBandEigs, "BandEigs",
	DumpEcomponents, "Ecomponents",
	DumpExcCompare, "ExcCompare",
	DumpBoundCharge, "BoundCharge",
	DumpQMC, "QMC",
	DumpRealSpaceWfns, "RealSpaceWfns",
	DumpFluidDebug, "FluidDebug",
	DumpSpinOrbit, "SpinOrbit",
	DumpProjectors, "Projectors",
	DumpWannier, "Wannier",
	DumpOptVext, "optVext",
	DumpDOS, "DOS",
	DumpSIC, "SelfInteractionCorrection",
	DumpDipole, "Dipole"
);
EnumStringMap<DumpVariable> varDescMap
(	DumpAll,            "Dump most things (except those marked not in All)",
	DumpNone,           "Dump nothing",
	DumpState,          "All variables needed to restart calculation: wavefunction and fluid state/fillings if any",
	DumpIonicPositions, "Ionic positions in the same format (and coordinate system) as the input file",
	DumpForces,         "Forces on the ions in the coordinate system selected by command forces-output-coords",
	DumpLattice,        "Lattice vectors in the same format as the input file",
	DumpIonicDensity,   "Nuclear charge density (with gaussians)",
	DumpElecDensity,    "Electronic densities (n or nup,ndn)",
	DumpCoreDensity,    "Total core electron density (from partial core corrections)",
	DumpKEdensity,      "Kinetic energy density of the valence electrons",
	DumpFluidDensity,   "Fluid densities (NO,NH,nWater for explicit fluids, cavity function for PCMs)",
	DumpDvac,           "Electrostatic potential due to explicit system alone",
	DumpDfluid,         "Electrostatic potential due to fluid alone",
	DumpDtot,           "Total electrostatic potential",
	DumpVcavity,        "Fluid cavitation potential on the electron density that determines the cavity",
	DumpVfluidTot,      "Total contribution of fluid to the electron potential",
	DumpVlocps,         "Local part of pseudopotentials",
	DumpVscloc,         "Self-consistent potential",
	DumpHsubEvecs,      "Subspace hamiltonian and eigenvectors",
	DumpBandEigs,       "Band Eigenvalues",
	DumpEcomponents,    "Components of the energy",
	DumpBoundCharge,    "Bound charge in the fluid",
	DumpQMC,            "Blip'd orbitals and potential for CASINO",
	DumpRealSpaceWfns,  "Real-space wavefunctions (one column per file) [not in All]",
	DumpExcCompare,     "Energies for other exchange-correlation functionals (see elec-ex-corr-compare) [not in All]",
	DumpFluidDebug,     "Fluid specific debug output if any  [not in All]",
	DumpSpinOrbit,      "Compute spin-orbit matrix elements [not in All]",
	DumpProjectors,     "Compute PAW projectors [not in All]",
	DumpWannier,        "Compute Maximally-Localized Wannier Functions (see wannier) [not in All]",
	DumpOptVext,        "Optimized external potentials (see invertKohnSham) [not in All]",
	DumpDOS,            "Density of States (see density-of-states) [not in All]",
	DumpSIC,            "Calculates Perdew-Zunger self-interaction corrected Kohn-Sham eigenvalues",
	DumpDipole,         "Dipole moment of explicit charges (ionic and electronic)"
);

struct CommandDump : public Command
{
	CommandDump() : Command("dump")
	{
		format = "<freq> <var> <var> ...";
		comments =
			"<freq> is one of:"
			+ addDescriptions(freqMap.optionList(), linkDescription(freqMap, freqDescMap))
			+ "\nand each <var*> is one of:"
			+ addDescriptions(varMap.optionList(), linkDescription(varMap, varDescMap))
			+ "\nList of dumped variables from multiple instances will be accumulated for each <freq>."
			"\nUse command dump-interval to dump at regular intervals instead of every iteration.";
		allowMultiple = true;
		hasDefault = true;
	}

	void process(ParamList& pl, Everything& e)
	{	DumpFrequency freq;
		pl.get(freq, DumpFreq_Delim, freqMap, "freq");
		//Handle the default call
		if(freq==DumpFreq_Delim)
		{	e.dump.insert(std::make_pair(DumpFreq_End,DumpState));
			return;
		}
		//For any real dump frequency:
		while(true)
		{	DumpVariable var;
			pl.get(var, DumpDelim, varMap, "var");
			if(var==DumpDelim) break; //will happen at end of command line
			e.dump.insert(std::make_pair(freq,var));
		}
	}

	void printStatus(Everything& e, int iRep)
	{	//Coealesce dump outputs for each frequency into a single command
		std::multimap<DumpFrequency,DumpVariable> dumpMap(e.dump.begin(), e.dump.end());
		typedef std::multimap<DumpFrequency,DumpVariable>::iterator Iter;
		Iter i = dumpMap.begin();
		int iDump=0;
		while(i!=dumpMap.end())
		{	if(iDump==iRep) logPrintf("%s", freqMap.getString(i->first));
			std::pair<Iter,Iter> range = dumpMap.equal_range(i->first);
			for(i=range.first; i!=range.second; i++)
				if(iDump==iRep) logPrintf(" %s", varMap.getString(i->second));
			iDump++;
		}
	}
}
commandDump;


struct CommandDumpInterval : public Command
{
	CommandDumpInterval() : Command("dump-interval")
	{
		format = "<freq> <interval>";
		comments = 
			"Dump every <interval> iterations of type <freq>=Ionic|Electronic|Fluid|Gummel\n"
			"Without this command, the behavior defaults to <interval>=1 for each <freq>.";
		allowMultiple = true;
	}

	void process(ParamList& pl, Everything& e)
	{	//get the frequency:
		DumpFrequency freq;
		pl.get(freq, DumpFreq_Delim, freqMap, "freq", true);
		if(freq==DumpFreq_End)
			throw string("<freq> must be one of Ionic|Electronic|Fluid|Gummel");
		if(e.dump.interval.find(freq) != e.dump.interval.end())
			throw string("dump-interval has been specified multiple times for <freq>=") + freqMap.getString(freq);
		//get the interval:
		int interval;
		pl.get(interval, 1, "interval", true);
		if(interval<1)
			throw string("<interval> must be a positive integer");
		//Set the interval
		e.dump.interval[freq] = interval;
	}

	void printStatus(Everything& e, int iRep)
	{	std::map<DumpFrequency,int>::const_iterator iter = e.dump.interval.begin();
		for(int i=0; i<iRep; i++) iter++; //access the iRep'th entry of interval
		logPrintf("%s %d", freqMap.getString(iter->first), iter->second);
	}
}
commandDumpInterval;


struct CommandDumpName : public Command
{
	CommandDumpName() : Command("dump-name")
	{
		format = "<format>";
		comments = 
			"  Control the filename pattern for dump output:\n"
			"    <format> is an arbitrary format string that will be substituted according to:\n"
			"       $VAR -> name of the variable being dumped (this must be present somewhere in the string)\n"
			"       $STAMP -> time-stamp at the start of dump";
		hasDefault = true;
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.dump.format, string("$STAMP.$VAR"), "format");
		if(e.dump.format.find("$VAR")==string::npos)
			throw "<format> = " + e.dump.format + " doesn't contain the pattern $VAR";
	}

	void printStatus(Everything& e, int iRep)
	{	logPrintf("%s", e.dump.format.c_str());
	}
}
commandDumpName;


EnumStringMap<Polarizability::EigenBasis> polarizabilityMap
(	Polarizability::NonInteracting, "NonInteracting",
	Polarizability::External, "External",
	Polarizability::Total, "Total"
);

struct CommandPolarizability : public Command
{
    CommandPolarizability() : Command("polarizability")
	{
		format = "<eigenBasis>=" + polarizabilityMap.optionList() + " [<Ecut>=0] [<nEigs>=0]";
		comments = "Output polarizability matrix in specified eigeneigenBasis";
	}
	
	void process(ParamList& pl, Everything& e)
	{	e.dump.polarizability = std::make_shared<Polarizability>();
		pl.get(e.dump.polarizability->eigenBasis, Polarizability::NonInteracting, polarizabilityMap, "eigenBasis");
		pl.get(e.dump.polarizability->Ecut, 0., "Ecut");
		pl.get(e.dump.polarizability->nEigs, 0, "nEigs");
		e.dump.insert(std::make_pair(DumpFreq_End, DumpPolarizability));
	}

	void printStatus(Everything& e, int iRep)
	{	logPrintf("%s %lg %d", polarizabilityMap.getString(e.dump.polarizability->eigenBasis),
			e.dump.polarizability->Ecut, e.dump.polarizability->nEigs);
	}
}
commandPolarizability;

struct CommandPolarizabilityKdiff : public Command
{
    CommandPolarizabilityKdiff() : Command("polarizability-kdiff")
	{
		format = "<dk0> <dk1> <dk2> [<dkFilenamePattern>]";
		comments = "Select k-point difference (in reciprocal lattice coords) for polarizability output.\n"
			"<dkFilenamePattern> may be specified to read offset band structure calcualations when <dk>\n"
			"does not belong to the k-point mesh. This string should be a filename pattern containing\n"
			"$VAR (to be replaced by eigenvals and wfns) and $q (to be replaced by state index)";
		
		require("polarizability");
	}
	
	void process(ParamList& pl, Everything& e)
	{	pl.get(e.dump.polarizability->dk[0], 0., "dk0", true);
		pl.get(e.dump.polarizability->dk[1], 0., "dk1", true);
		pl.get(e.dump.polarizability->dk[2], 0., "dk2", true);
		//Optional filename for offset states:
		string& dkFilenamePattern = e.dump.polarizability->dkFilenamePattern;
		pl.get(dkFilenamePattern, string(), "dkFilenamePattern");
		if(dkFilenamePattern.find("$VAR")==string::npos) throw "<dkFilenamePattern> = " + dkFilenamePattern + " doesn't contain the pattern $VAR";
		if(dkFilenamePattern.find("$q")==string::npos) throw "<dkFilenamePattern> = " + dkFilenamePattern + " doesn't contain the pattern $q";
	}

	void printStatus(Everything& e, int iRep)
	{	for(int i=0; i<3; i++) logPrintf("%lg ", e.dump.polarizability->dk[i]);
		logPrintf("%s", e.dump.polarizability->dkFilenamePattern.c_str());
	}
}
commandPolarizabilityKdiff;
