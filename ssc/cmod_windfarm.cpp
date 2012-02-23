#include "core.h"
#include "lib_weatherfile.h"
#include "lib_windwatts.h"

static var_info _cm_vtab_windfarm[] = {
	{ SSC_INPUT,        SSC_STRING,      "file_name",                  "local SWRF file path",		       "",       "",                      "Weather",      "*",               "LOCAL_FILE",            "" },
		
	{ SSC_INPUT,        SSC_NUMBER,      "shear",                      "Shear exponent",                   "",       "",                      "WindFarm",      "*",             "",                      "" },
	{ SSC_INPUT,        SSC_NUMBER,      "turbul",                     "Turbulence coefficient",           "frac",   "",                      "WindFarm",      "*",             "",                      "" },
	{ SSC_INPUT,        SSC_ARRAY,       "pc_wind",                    "Power curve wind speed array",     "0/1/2",  "",                      "WindFarm",      "*",             "",                      "" }, 
	{ SSC_INPUT,        SSC_ARRAY,       "pc_power",                   "Power curve turbine output array", "deg",    "",                      "WindFarm",      "*",             "LENGTH_EQUAL=pc_wind",  "" },
	{ SSC_INPUT,        SSC_ARRAY,       "wt_x",                       "Turbine X coordinates",            "m",      "",                      "WindFarm",      "*",             "",                      "" },
	{ SSC_INPUT,        SSC_ARRAY,       "wt_y",                       "Turbine Y coordinates",            "m",      "",                      "WindFarm",      "*",             "LENGTH_EQUAL=wt_x",     "" },
	{ SSC_INPUT,        SSC_NUMBER,      "hub_ht",                     "Hub height",                       "m",      "",                      "WindFarm",      "*",             "",                      "" },
	{ SSC_INPUT,        SSC_NUMBER,      "rotor_di",                   "Rotor diameter",                   "m",      "",                      "WindFarm",      "*",             "",                      "" },
	//{ SSC_INPUT,        SSC_NUMBER,      "ctl_mode",                   "Control mode",                     "0/1/2",  "",                      "WindFarm",      "*",             "",                      "" },
	{ SSC_INPUT,        SSC_NUMBER,      "cutin",                      "Cut-in wind speed",                "m/s",    "",                      "WindFarm",      "*",             "",                      "" },
	{ SSC_INPUT,        SSC_NUMBER,      "lossc",                      "Constant losses",                  "kW",     "",                      "WindFarm",      "*",             "",                      "" },
	{ SSC_INPUT,        SSC_NUMBER,      "lossp",                      "Percentage losses",                "%",      "",                      "WindFarm",      "*",             "",                      "" },
	{ SSC_INPUT,        SSC_NUMBER,      "meas_ht",                    "Height of resource measurement",   "m",      "",                      "WindFarm",      "*",             "INTEGER",               "" },
	
	{ SSC_OUTPUT,       SSC_ARRAY,       "farmpwr",                    "Net electric generation",          "kWhac",  "",                      "WindFarm",      "*",             "LENGTH=8760",     "" },
	{ SSC_OUTPUT,       SSC_ARRAY,       "winddir",                    "Wind direction",                   "deg",    "",                      "WindFarm",      "*",             "LENGTH=8760",     "" },
	{ SSC_OUTPUT,       SSC_ARRAY,       "windspd",                    "Wind speed",                       "m/s",    "",                      "WindFarm",      "*",             "LENGTH=8760",     "" },
	{ SSC_OUTPUT,       SSC_MATRIX,      "wtpwr",                      "Power at each WT",                 "kWhac",  "",                      "WindFarm",      "*",             "ROWS=8760",       "" },
	{ SSC_OUTPUT,       SSC_MATRIX,      "wteff",                      "Eff at each WT",                   "kWhac",  "",                      "WindFarm",      "*",             "ROWS=8760",       "" },
	{ SSC_OUTPUT,       SSC_MATRIX,      "wtvel",                      "Wind speed at each WT",            "kWhac",  "",                      "WindFarm",      "*",             "ROWS=8760",       "" },



var_info_invalid };

class cm_windfarm : public compute_module
{
private:
public:
	
	cm_windfarm()
	{
		add_var_info( _cm_vtab_windfarm );
	}

	void exec( ) throw( general_error )
	{
		const char *file = as_string("file_name");
		
		weatherfile wf(file);		
		if (!wf.ok()) throw exec_error("windfarm", "failed to read local weather file: " + std::string(file));

		size_t pc_len = 0;
		ssc_number_t *pc_w = as_array( "pc_wind", &pc_len );
		ssc_number_t *pc_p = as_array( "pc_power", NULL );

		size_t nwt = 0;
		ssc_number_t *wt_x = as_array( "wt_x", &nwt );
		ssc_number_t *wt_y = as_array( "wt_y", NULL );

		double shear = as_double("shear");
		double turbul = as_double("turbul");
		double hub_ht = as_double("hub_ht");
		double rotor_di = as_double("rotor_di");
		double meas_ht = as_double("meas_ht");
		wf.resource_ht = as_integer("meas_ht");

		/* ctl_mode hardwired to '2'.  apparently not implemented 
		  correctly for modes 0 and 1, so no point exposing it.
		  apd 03jan11 */

		int ctl_mode = 2; // as_integer("ctl_mode");
		double cutin = as_double("cutin");
		double lossc = as_double("lossc");
		double lossp = as_double("lossp");

		std::vector<double> Dn(nwt), Cs(nwt), 
			Power(nwt), Thrust(nwt), Eff(nwt), 
			Wind(nwt), Turb(nwt), 
			
			X(nwt), Y(nwt),
			dpcW(pc_len), dpcP(pc_len);

		size_t i,j;

		for (i=0;i<nwt;i++)
		{
			X[i] = (double)wt_x[i];
			Y[i] = (double)wt_y[i];
		}

		for (i=0;i<pc_len;i++)
		{
			dpcW[i] = (double)pc_w[i];
			dpcP[i] = (double)pc_p[i];
		}

		ssc_number_t *farmpwr = allocate( "farmpwr", 8760 );
		ssc_number_t *wspd = allocate("windspd", 8760);
		ssc_number_t *wdir = allocate("winddir", 8760);
		util::matrix_t<ssc_number_t> &mat_wtpwr = allocate_matrix( "wtpwr", 8760, nwt );
		util::matrix_t<ssc_number_t> &mat_wteff = allocate_matrix( "wteff", 8760, nwt );
		util::matrix_t<ssc_number_t> &mat_wtvel = allocate_matrix( "wtvel", 8760, nwt );
		util::matrix_t<ssc_number_t> &mat_dn = allocate_matrix("dn", 8760, nwt );
		util::matrix_t<ssc_number_t> &mat_cs = allocate_matrix("cs", 8760, nwt );
		
		
		double last_wind, last_theta, last_tdry, last_pres, wind, theta, tdry, pres;
		wf.read();
		wind = last_wind = wf.wspd;
		theta = last_theta = wf.wdir;
		tdry = last_tdry = wf.tdry;
		pres = last_pres = wf.pres;

		for (i=0;i<8760;i++)
		{
			
			double farmp = 0;

			if ( (int)nwt != wind_power( 
						/* inputs */
						wind,
						theta,
						shear,
						turbul,
						pres,  /* Atm */
						tdry,
						(int)nwt,
						&X[0],
						&Y[0],
						(int)pc_len,
						&dpcW[0],
						&dpcP[0],
						meas_ht, /* TFF - what if they're not using TMY2? 10.0, /*10 meter data measure height in TMY2 */
						hub_ht,
						rotor_di,
						ctl_mode,
						cutin,
						0,
						0,
						lossc,
						lossp,

						/* outputs */
						&farmp,
						&Dn[0],
						&Cs[0],
						&Power[0],
						&Thrust[0],
						&Eff[0],
						&Wind[0],
						&Turb[0] ) ) 
				throw exec_error( "windfarm", util::format("error in wind calculation time %d", i) );


			farmpwr[i] = (ssc_number_t) farmp;
			wspd[i] = (ssc_number_t) wind;
			wdir[i] = (ssc_number_t) theta;

			for (j=0;j<nwt;j++)
			{
				mat_dn.at(i,j) = (ssc_number_t)Dn[j];
				mat_cs.at(i,j) = (ssc_number_t)Cs[j];
				mat_wtpwr.at(i,j) = (ssc_number_t) Power[j];
				mat_wteff.at(i,j) = (ssc_number_t) Eff[j];
				mat_wtvel.at(i,j) = (ssc_number_t) Wind[j];
			}

			
			if (i < 8759)
			{
				if (!wf.read())
					throw exec_error("windfarm", "could not read data line " + util::to_string((int)i+1) + " of 8760");
				
				wind = (last_wind+wf.wspd)/2.0;
				theta = (last_theta+wf.wdir)/2.0;
				tdry = (last_tdry+wf.tdry)/2.0;
				pres = (last_pres+wf.pres)/2.0;
				last_wind = wf.wspd;
				last_theta = wf.wdir;
				last_tdry = wf.tdry;
				last_pres = wf.pres;
			}

		}
	}

};

DEFINE_MODULE_ENTRY( windfarm, "Utility scale wind farm model (ported from original TRNSYS P.Quinlan)", 2 );

