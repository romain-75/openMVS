////////////////////////////////////////////////////////////////////
// Common.cpp
//
// Copyright 2007 cDc@seacave
// Distributed under the Boost Software License, Version 1.0
// (See http://www.boost.org/LICENSE_1_0.txt)

// Source file that includes just the standard includes
// Common.pch will be the pre-compiled header
// Common.obj will contain the pre-compiled type information

#include "Common.h"

void outputLogSQL(std::string nature, std::string chaine1, std::string chaine2, int chaine3, std::string chaine4, bool logEvenementiel){

    static time_t precedentLog = time(nullptr);
    time_t tempsActuel = time(nullptr);

    if (logEvenementiel || tempsActuel-precedentLog >= 5){ // toutes les 5 secondes minimum, sauf log evenementiel

        if (!logEvenementiel)
            precedentLog = tempsActuel;

        std::string chaine3_txt = std::to_string(chaine3);

#ifdef __APPLE__
        std::string commande = "python logger-tns-MARS-Adapte.py "+nature+" "+chaine1+" "+chaine2+ " "+chaine3_txt+ " \""+chaine4+"\" &";
#else
#ifdef _WIN32
        //std::string commande = "python3.exe logger-tns-MARS-Adapte.py "+nature+" "+chaine1+" "+chaine2+ " "+chaine3_txt+ " \""+chaine4+"\"";
        //std::string commande = "start \"\" cmd /c python3.exe logger-tns-MARS-Adapte.py "+nature+" "+chaine1+" "+chaine2+ " "+chaine3_txt+ " \""+chaine4+"\"";
        std::string commande = "\"python.exe logger-tns-MARS-Adapte.py "+nature+" "+chaine1+" "+chaine2+ " "+chaine3_txt+ " \""+chaine4+"\"";
#else
        std::string commande = "python3 logger-tns-MARS-Adapte.py " +nature+" "+chaine1+" "+chaine2+ " "+chaine3_txt+ " \""+chaine4+"\" &";
#endif
#endif
        system(commande.c_str());
    }
}

namespace SEACAVE {
#if TD_VERBOSE == TD_VERBOSE_ON
int g_nVerbosityLevel(2);
#endif
#if TD_VERBOSE == TD_VERBOSE_DEBUG
int g_nVerbosityLevel(3);
#endif

String g_strWorkingFolder;
String g_strWorkingFolderFull;
} // namespace SEACAVE

#ifdef _USE_BOOST
#ifdef BOOST_NO_EXCEPTIONS
#if (BOOST_VERSION / 100000) > 1 || (BOOST_VERSION / 100 % 1000) > 72
#include <boost/assert/source_location.hpp>
#endif
namespace boost {
	void throw_exception(std::exception const & e) {
		VERBOSE("exception thrown: %s", e.what());
		ASSERT("boost exception thrown" == NULL);
		exit(EXIT_FAILURE);
	}
	#if (BOOST_VERSION / 100000) > 1 || (BOOST_VERSION / 100 % 1000) > 72
	void throw_exception(std::exception const & e, boost::source_location const & loc) {
		std::ostringstream ostr; ostr << loc;
		VERBOSE("exception thrown at %s: %s", ostr.str().c_str(), e.what());
		ASSERT("boost exception thrown" == NULL);
		exit(EXIT_FAILURE);
	}
	#endif
} // namespace boost
#endif
#endif
