/*
 * Viewer.cpp
 *
 * Copyright (c) 2014-2015 SEACAVE
 *
 * Author(s):
 *
 *      cDc <cdc.seacave@gmail.com>
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Additional Terms:
 *
 *      You are required to preserve legal notices and author attributions in
 *      that material or in the Appropriate Legal Notices displayed by works
 *      containing it.
 */

#include "Common.h"
#include <boost/program_options.hpp>

#include "Scene.h"

using namespace VIEWER;


// D E F I N E S ///////////////////////////////////////////////////

#define APPNAME _T("Viewer")


// S T R U C T S ///////////////////////////////////////////////////

namespace {

namespace OPT {
String strInputFileName;
String strGeometryFileName;
String strOutputFileName;
unsigned nArchiveType;
int nProcessPriority;
unsigned nMaxThreads;
unsigned nMaxMemory;
String strExportType;
String strConfigFileName;
#if TD_VERBOSE != TD_VERBOSE_OFF
bool bLogFile;
#endif
boost::program_options::variables_map vm;
} // namespace OPT

class Application {
public:
	Application() {}
	~Application() { Finalize(); }

	bool Initialize(size_t argc, LPCTSTR* argv);
	void Finalize();
}; // Application

// initialize and parse the command line parameters
bool Application::Initialize(size_t argc, LPCTSTR* argv)
{
	// initialize log and console
	OPEN_LOG();
	OPEN_LOGCONSOLE();

	// group of options allowed only on command line
	boost::program_options::options_description generic("Generic options");
	generic.add_options()
		("help,h", "produce this help message")
		("working-folder,w", boost::program_options::value<std::string>(&WORKING_FOLDER), "working directory (default current directory)")
		("config-file,c", boost::program_options::value<std::string>(&OPT::strConfigFileName)->default_value(APPNAME _T(".cfg")), "file name containing program options")
		("export-type", boost::program_options::value<std::string>(&OPT::strExportType), "file type used to export the 3D scene (ply or obj)")
		("archive-type", boost::program_options::value(&OPT::nArchiveType)->default_value(ARCHIVE_MVS), "project archive type: -1-interface, 0-text, 1-binary, 2-compressed binary")
		("process-priority", boost::program_options::value(&OPT::nProcessPriority)->default_value(0), "process priority (normal by default)")
		("max-threads", boost::program_options::value(&OPT::nMaxThreads)->default_value(0), "maximum number of threads that this process should use (0 - use all available cores)")
		("max-memory", boost::program_options::value(&OPT::nMaxMemory)->default_value(0), "maximum amount of memory in MB that this process should use (0 - use all available memory)")
		#if TD_VERBOSE != TD_VERBOSE_OFF
		("log-file", boost::program_options::value(&OPT::bLogFile)->default_value(false), "dump log to a file")
		("verbosity,v", boost::program_options::value(&g_nVerbosityLevel)->default_value(
			#if TD_VERBOSE == TD_VERBOSE_DEBUG
			3
			#else
			2
			#endif
			), "verbosity level")
		#endif
		;

	// group of options allowed both on command line and in config file
	boost::program_options::options_description config("Viewer options");
	config.add_options()
		("input-file,i", boost::program_options::value<std::string>(&OPT::strInputFileName), "input project filename containing camera poses and scene (point-cloud/mesh)")
		("geometry-file,g", boost::program_options::value<std::string>(&OPT::strGeometryFileName), "mesh or point-cloud with views file name (overwrite existing geometry)")
		("output-file,o", boost::program_options::value<std::string>(&OPT::strOutputFileName), "output filename for storing the mesh")
		;

	boost::program_options::options_description cmdline_options;
	cmdline_options.add(generic).add(config);

	boost::program_options::options_description config_file_options;
	config_file_options.add(config);

	boost::program_options::positional_options_description p;
	p.add("input-file", -1);

	try {
		// parse command line options
		boost::program_options::store(boost::program_options::command_line_parser((int)argc, argv).options(cmdline_options).positional(p).run(), OPT::vm);
		boost::program_options::notify(OPT::vm);
		INIT_WORKING_FOLDER;
		// parse configuration file
		std::ifstream ifs(MAKE_PATH_SAFE(OPT::strConfigFileName));
		if (ifs) {
			boost::program_options::store(parse_config_file(ifs, config_file_options), OPT::vm);
			boost::program_options::notify(OPT::vm);
		}
	}
	catch (const std::exception& e) {
		LOG(e.what());
		return false;
	}

	#if TD_VERBOSE != TD_VERBOSE_OFF
	// initialize the log file
	if (OPT::bLogFile)
		OPEN_LOGFILE((MAKE_PATH(APPNAME _T("-")+Util::getUniqueName(0)+_T(".log"))).c_str());
	#endif

	// print application details: version and command line
	Util::LogBuild();
	LOG(_T("Command line: ") APPNAME _T("%s"), Util::CommandLineToString(argc, argv).c_str());

	// validate input
	Util::ensureValidPath(OPT::strInputFileName);
	if (OPT::vm.count("help")) {
		boost::program_options::options_description visible("Available options");
		visible.add(generic).add(config);
		GET_LOG() << _T("\n"
			"Visualize any know point-cloud/mesh formats or MVS projects. Supply files through command line or Drag&Drop.\n"
			"Keys:\n"
			"\tE: export scene\n"
			"\tR: reset scene\n"
			"\tB: render bounds\n"
			"\tB + Shift: togle bounds\n"
			"\tC: render cameras\n"
			"\tC + Shift: render camera trajectory\n"
			"\tC + Ctrl: center scene\n"
			"\tLeft/Right: select next camera to view the scene\n"
			"\tS: save scene\n"
			"\tS + Shift: rescale images and save scene\n"
			"\tT: render mesh texture\n"
			"\tW: render wire-frame mesh\n"
			"\tV: render view rays to the selected point\n"
			"\tV + Shift: render points seen by the current view\n"
			"\tUp/Down: adjust point size\n"
			"\tUp/Down + Shift: adjust minimum number of views accepted when displaying a point or line\n"
			"\t+/-: adjust camera thumbnail transparency\n"
			"\t+/- + Shift: adjust camera cones' length\n"
			"\t+/- + Ctrl: adjust camera FOV\n"
			"\t+/- + Alt: adjust points confidence visibility threshold\n"
			"\n")
			<< visible;
	}
	if (!OPT::strExportType.empty())
		OPT::strExportType = OPT::strExportType.ToLower() == _T("obj") ? _T(".obj") : _T(".ply");

	// initialize optional options
	Util::ensureValidPath(OPT::strGeometryFileName);
	Util::ensureValidPath(OPT::strOutputFileName);

	MVS::Initialize(APPNAME, OPT::nMaxThreads, OPT::nProcessPriority);
	return true;
}

// finalize application instance
void Application::Finalize()
{
	MVS::Finalize();

	if (OPT::bLogFile)
		CLOSE_LOGFILE();
	CLOSE_LOGCONSOLE();
	CLOSE_LOG();
}

} // unnamed namespace

int main(int argc, LPCTSTR* argv)
{
	#ifdef _DEBUGINFO
	// set _crtBreakAlloc index to stop in <dbgheap.c> at allocation
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);// | _CRTDBG_CHECK_ALWAYS_DF);
	#endif

	Application application;
	if (!application.Initialize(argc, argv))
		return EXIT_FAILURE;

	// create viewer
	Scene viewer;
	if (!viewer.Init(cv::Size(1280, 720), APPNAME,
			OPT::strInputFileName.empty() ? NULL : MAKE_PATH_SAFE(OPT::strInputFileName).c_str(),
			OPT::strGeometryFileName.empty() ? NULL : MAKE_PATH_SAFE(OPT::strGeometryFileName).c_str()))
		return EXIT_FAILURE;
	if (viewer.IsOpen() && !OPT::strOutputFileName.empty()) {
		// export the scene
		viewer.Export(MAKE_PATH_SAFE(OPT::strOutputFileName), OPT::strExportType.empty()?LPCTSTR(NULL):OPT::strExportType.c_str());
	}
	// enter viewer loop
	viewer.Loop();
	return EXIT_SUCCESS;
}
/*----------------------------------------------------------------*/
