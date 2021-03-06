/*******************************************************************************************************
*  Copyright 2017 Alliance for Sustainable Energy, LLC
*
*  NOTICE: This software was developed at least in part by Alliance for Sustainable Energy, LLC
*  (�Alliance�) under Contract No. DE-AC36-08GO28308 with the U.S. Department of Energy and the U.S.
*  The Government retains for itself and others acting on its behalf a nonexclusive, paid-up,
*  irrevocable worldwide license in the software to reproduce, prepare derivative works, distribute
*  copies to the public, perform publicly and display publicly, and to permit others to do so.
*
*  Redistribution and use in source and binary forms, with or without modification, are permitted
*  provided that the following conditions are met:
*
*  1. Redistributions of source code must retain the above copyright notice, the above government
*  rights notice, this list of conditions and the following disclaimer.
*
*  2. Redistributions in binary form must reproduce the above copyright notice, the above government
*  rights notice, this list of conditions and the following disclaimer in the documentation and/or
*  other materials provided with the distribution.
*
*  3. The entire corresponding source code of any redistribution, with or without modification, by a
*  research entity, including but not limited to any contracting manager/operator of a United States
*  National Laboratory, any institution of higher learning, and any non-profit organization, must be
*  made publicly available under this license for as long as the redistribution is made available by
*  the research entity.
*
*  4. Redistribution of this software, without modification, must refer to the software by the same
*  designation. Redistribution of a modified version of this software (i) may not refer to the modified
*  version by the same designation, or by any confusingly similar designation, and (ii) must refer to
*  the underlying software originally provided by Alliance as �System Advisor Model� or �SAM�. Except
*  to comply with the foregoing, the terms �System Advisor Model�, �SAM�, or any confusingly similar
*  designation may not be used to refer to any modified version of this software or any modified
*  version of the underlying software originally provided by Alliance without the prior written consent
*  of Alliance.
*
*  5. The name of the copyright holder, contributors, the United States Government, the United States
*  Department of Energy, or any of their employees may not be used to endorse or promote products
*  derived from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
*  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
*  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER,
*  CONTRIBUTORS, UNITED STATES GOVERNMENT OR UNITED STATES DEPARTMENT OF ENERGY, NOR ANY OF THEIR
*  EMPLOYEES, BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
*  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
*  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
*  THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************************************/

#include <stdarg.h>

#include "core.h"
#include "lib_weatherfile.h"
#include "lib_irradproc.h"
#include "lib_util.h"

static var_info _cm_vtab_wfcheck[] = 
{	
/*   VARTYPE           DATATYPE         NAME                         LABEL                              UNITS     META                      GROUP                     REQUIRED_IF                 CONSTRAINTS                      UI_HINTS*/
	{ SSC_INPUT,        SSC_STRING,      "input_file",               "Input weather file name",         "",       "wfcsv format",      "Weather File Checker", "*",                       "",                     "" },
	
var_info_invalid };
class cm_wfcheck : public compute_module
{
private:
	int nwarnings, nerrors;
public:
	cm_wfcheck()
	{
		nwarnings = nerrors = 0;
		add_var_info( _cm_vtab_wfcheck );
	}

	void warn( const char *fmt, ... )
	{
		char buf[1024];
		va_list ap;
		va_start(ap, fmt);
#if defined(_MSC_VER)||defined(_WIN32)
		_vsnprintf(buf, 1023, fmt, ap);
#else
		vsnprintf(buf, 1023, fmt, ap);
#endif
		va_end(ap);

		assign( util::format( "warning%d", nwarnings ), var_data(std::string(buf)) );
		nwarnings++;
	}

	void exec() throw( general_error )
	{
		weatherfile wfile( as_string("input_file") );
		if ( !wfile.ok() ) throw general_error( wfile.message() );
		if( wfile.has_message() ) log( wfile.message(), SSC_WARNING);

		weather_header hdr;
		wfile.header( &hdr );

		weather_record wf;

		nwarnings = nerrors = 0;

		double T = 60; // threshold on temp

		double zenith, hextra;
		double sunn[9];
		for( size_t i = 0; i<wfile.nrecords(); i++ )
		{
			if ( !wfile.read( &wf ) )
			{
				warn("error reading record %d, stopping", i );
				break;
			}			

			solarpos( wf.year, wf.month, wf.day, wf.hour, wf.minute, hdr.lat, hdr.lon, hdr.tz, sunn );						
			zenith = sunn[1]; // zenith angle, radians
			hextra = sunn[8];
			
			// check irradiance values for consistency if all three components exist
			double gh_est = wf.dn*cos(zenith) + wf.df;
			if ( std::isnan( gh_est ) )
				gh_est = wf.gh;

			if ( !std::isnan( wf.dn ) && !std::isnan( wf.df ) && !std::isnan( wf.gh ) )
			{
				if ( gh_est > 500 && fabs(gh_est-wf.gh)/wf.gh > 0.2 )
					warn( "beam+diffuse (%lg) inconsistent with global (%lg) at record %d by greater than 20 percent", gh_est, wf.gh, i );
				else if ( gh_est > 200 && fabs(gh_est-wf.gh)/wf.gh > 0.5 )
					warn( "beam+diffuse (%lg) inconsistent with global (%lg) at record %d by greater than 50 percent", gh_est, wf.gh, i );

				// don't do checks on less than 200 W/m2, errors are too big to really check
			}



			if ( !std::isnan( wf.dn ) && wf.dn > 1500 ) warn( "beam irradiance (%lg) at record %d is greater than 1500", wf.dn, i );
			if ( !std::isnan( wf.dn ) && wf.dn < 0 ) warn( "beam irradiance (%lg) at record %d is negative", wf.dn, i );
			
			// cap for global and diffuse irradiance 
			double irrmax = 1.5*(hextra+150);			
			if ( irrmax > 1500 ) irrmax = 1500;

			if ( !std::isnan( wf.df ) && wf.df > irrmax ) warn( "diffuse irradiance (%lg) at record %d is greater than threshold (%lg)", wf.df, i, irrmax );
			if ( !std::isnan( wf.df ) && wf.df < 0 ) warn( "diffuse irradiance (%lg) at record %d is negative", wf.df, i );
			
			if ( !std::isnan( wf.gh ) && wf.gh > irrmax ) warn( "global irradiance (%lg) at record %d is greater than threshold (%lg)", wf.gh, i, irrmax );
			if ( !std::isnan( wf.gh ) && wf.gh < 0 ) warn( "global irradiance (%lg) at record %d is negative", wf.gh, i );


			int nirrnans = 0;
			if ( std::isnan( wf.dn ) ) nirrnans++;
			if ( std::isnan( wf.gh ) ) nirrnans++;
			if ( std::isnan( wf.df ) ) nirrnans++;
			if ( nirrnans > 1 )
				warn( "[%lg %lg %lg] only 1 component of irradiance specified at record %d", wf.gh, wf.dn, wf.df, i );

			if ( wf.wspd > 30 ) warn( "wind speed (%lg) greater than 30 m/s at record %d", wf.wspd, i );
			if ( wf.wspd < 0 ) warn("wind speed (%lg) less than 0 m/s at record %d",wf.wspd, i );
			
			if ( wf.wdir > 360 ) warn("wind direction angle (%lg) greater than 360 degrees at record %d", wf.wdir, i );
			if ( wf.wdir < 0 ) warn( "wind direction angle (%lg) less than 0 degrees at record %d", wf.wdir, i );
			
			if ( wf.tdry > T ) warn( "dry bulb temperature (%lg) greater than %lg C at record %d", wf.tdry, T, i );
			if ( wf.tdry < -T ) warn( "dry bulb temperature (%lg) less than -%lg C at record %d", wf.tdry, T, i );
			
			if ( wf.twet > T ) warn( "wet bulb temperature (%lg) greater than %lg C at record %d", wf.twet, T, i );
			if ( wf.twet < -T ) warn( "wet bulb temperature (%lg) less than -%lg C at record %d", wf.twet, T, i );
			
			if ( wf.tdew > T ) warn( "dew point temperature (%lg) greater than %lg C at record %d", wf.tdew, T, i );
			if ( wf.tdew < -T ) warn( "dew point temperature (%lg) less than -%lg C at record %d", wf.tdew, T, i );

			if ( wf.rhum < 2 ) warn("relative humidity (%lg) less than 2 percent at record %d", wf.rhum, i );
			if ( wf.rhum > 100 ) warn("relative humidity (%lg) greater than 100 percent at record %d", wf.rhum, i );

			if ( wf.pres < 200 ) warn("pressure (%lg) less than 200 millibar at record %d", wf.pres, i );
			if ( wf.pres > 1100 ) warn("pressure greater than 1100 millibar at record %d", wf.pres, i );


			if ( nwarnings >= 99 )
			{
				warn( "bailing... too many warnings.");
				break;
			}

		}



		assign("nwarnings", var_data((ssc_number_t)nwarnings));
	}
};

DEFINE_MODULE_ENTRY( wfcheck, "Weather file checker.", 1 );
