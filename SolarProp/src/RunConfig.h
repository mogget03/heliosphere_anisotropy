/*
 * Lightweight container for run parameters.
 */

#ifndef RUNCONFIG_H
#define RUNCONFIG_H

#include "Params.h"
#include "PPTrajectory.h"
#include <string>
#include <vector>

// TODO: add outputPrefix to RunConfig!

class RunConfig {
	public:
		/*
		 * Reads run configuration from a csv file.
		 * Arguments:
		 *	rcFileName: the csv file containing the run configuration parameters.  If any are missing or
		 *		of the wrong form, a RunConfigException will be thrown.  
		 *		Note that ri can be < 0, thi can lie outside of [0, pi) and phii can lie outside of [0, 2pi).
		 *		Such coordinates will be fixed when the trajectory is constructed.
		 */
		RunConfig(const std::string& rcFileName) : startPoint(rcFileName, {"ri", "thi", "phii", "eis"}),
				intParams(rcFileName, {"threads_per_energy", "runs_per_thread"}),
				flags(rcFileName, {"output_format", "output_dir"}) {
			// Must have at least one thread per energy
			if (intParams["threads_per_energy"] <= 0) {
				throw ParamInvalidException(rcFileName, "threads_per_energy");
			}

			// Must run at least one MC per thread
			if (intParams["runs_per_thread"] <= 0) {
				throw ParamInvalidException(rcFileName, "runs_per_thread");
			}

			// The output format must specify whether to record all points or just the first and last
			if (flags["output_format"] == "all") {
                outputFormat = OutputFormat::All;
            } else if (flags["output_format"] == "firstlast") {
                outputFormat = OutputFormat::FirstLast;
            } else {
				throw ParamInvalidException(rcFileName, "output_format");
			}
			
			// TODO: make sure output_dir exists!  Throw exception if it doesn't.
            if (flags["output_dir"].back() != '/') {
                flags["output_dir"] += "/";
            }

			// Must run at least one MC per thread
			if (startPoint["eis"] <= 0) {
				throw ParamInvalidException(rcFileName, "eis");
			} else {
				// If all of these parameters are valid, read the energies
				// TODO: read a list of energies
				eis.push_back(startPoint["eis"]);
			}
		};

		// Getters
		double getR() const { return startPoint["ri"]; };
		double getTh() const { return startPoint["thi"]; };
		double getPhi() const { return startPoint["phii"]; };

		const std::vector<double>& getEis() const { return eis; };

		int getThreadsPerEnergy() const { return intParams["threads_per_energy"]; };
		int getRunsPerThread() const { return intParams["runs_per_thread"]; };

		const std::string& getOutputDir() const { return flags["output_dir"]; };

		// All parameters come from the same file
		const std::string& getRunParamFileName() const { return startPoint.getParamFileName(); };

        const OutputFormat& getOutputFormat() const { return outputFormat; };

		// TODO: write ALL eis to XML!
        /*
		std::string toXML() const {
			return "\t<run_configs>\n" + startPoint.toXML() + intParams.toXML() + flags.toXML()
				+ "\t</params>\n";
		};
        */

	private:
		// Initial energies to simulate
		std::vector<double> eis;

		// Contains coordinates at which to begin SDE integration
		Params<double> startPoint;

		// Contains number of threads to run per energy and number of simulations to carry out in each thread
		Params<int> intParams;

		// Contains directory
		Params<std::string> flags;

        // Enum value specifying output format
        OutputFormat outputFormat;
};

#endif


