/*
 * main.cpp: test example to run clODE and clODEtrajectory.cpp
 * 
 * Copyright 2017 Patrick Fletcher <patrick.fletcher@nih.gov>
 * 
 */
 
 //TODO what is the right way to do unit testing?
 
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <iostream>
#include <fstream>

#include "OpenCLResource.hpp"
#include "CLODE.hpp"
#include "CLODEtrajectory.hpp"



//Generate random points within given bounds
template<typename T> std::vector<T> generateRandomPoints(std::vector<T> lb, std::vector<T> ub, int nPts);

//currently the only command line arguments are to select device/vendor type ("--device cpu/gpu/accel", "--vendor amd/intel/nvidia")
int main(int argc, char **argv)
{
	try 
	{
 	cl_int nPts=32;
	bool CLSinglePrecision=true;
	
	ProblemInfo prob;
	prob.clRHSfilename="C:/Users/fletcherpa/Documents/GitHub/clODE/samples/lactotroph.cl";
	prob.nVar=4;
	prob.nPar=3;
	prob.nAux=1;
	prob.nWiener=0;
	prob.varNames.assign({"v","n","f","c"});
	prob.parNames.assign({"gcal","gsk","gbk"});
	prob.auxNames.assign({"ical"});
	
	// std::string stepper="rk4";
	std::string stepper="dopri5";
	
	//parameters for solver and objective function
	
	std::vector<double> tspan({0.0,1000.0});
	int nReps=1;

	SolverParams<double> sp;
	sp.dt=0.5;
	sp.dtmax=100.0;
	sp.abstol=1e-6;
	sp.reltol=1e-3;
	sp.max_steps=1000000;
	sp.max_store=100000;
	sp.nout=1;
	
	int mySeed=1;

	//default pars
	std::vector<double> p({1.5,3.0,1.0}); 
	
	// repeat the parameters nPts times: pack each paramater contiguously
	std::vector<double> pars(nPts, p[0]);
	pars.insert(pars.end(), nPts, p[1]);
	pars.insert(pars.end(), nPts, p[2]);

	// //Parameter sets will be sampled uniformly from [lb,ub] for each parameter
	// std::vector<double> lb({1.0,5.0,1.0});
	// std::vector<double> ub({1.0,5.0,1.0});
	// std::vector<double> pars=generateRandomPoints(lb, ub, nPts);
	
	//initial values: all zeros
	std::vector<double> x0(nPts*prob.nVar, 0.0);
	
	
//initialize opencl (several device selection options are commented out below)
	// unsigned int platformid = 0;
	// unsigned int deviceid = 0;
	
	// Default constructor: selects first OpenCL device found
	//~ OpenCLResource opencl;
	
	// Select device type and/or vendor using command line flags ("--device cpu/gpu/accel", "--vendor amd/intel/nvidia")
	OpenCLResource opencl( argc, argv);
	
	// Select device type/vendor programmatically
	//~ cl_device_type deviceType=CL_DEVICE_TYPE_DEFAULT;
	//~ cl_device_type deviceType=CL_DEVICE_TYPE_CPU;
	//~ cl_device_type deviceType=CL_DEVICE_TYPE_GPU;
	//~ cl_device_type deviceType=CL_DEVICE_TYPE_ACCELERATOR;
	//~ cl_device_type deviceType=CL_DEVICE_TYPE_ALL;
	
	//~ cl_vendor vendor=VENDOR_ANY;
	//~ cl_vendor vendor=VENDOR_AMD;
	//~ cl_vendor vendor=VENDOR_INTEL;
	//~ cl_vendor vendor=VENDOR_NVIDIA;
	
	//~ OpenCLResource opencl(deviceType); //default vendor=VENDOR_ANY
	//~ OpenCLResource opencl(deviceType, vendor);
	
	//prep timer and PRNG
	srand(static_cast <unsigned> (time(0))); 
    std::chrono::time_point<std::chrono::high_resolution_clock> start, end;
	std::chrono::duration<double, std::milli> elapsed_ms;
	
	// create the solver
	CLODEtrajectory clo(prob, stepper, CLSinglePrecision, opencl);
	// CLODEtrajectory clo(prob, stepper, CLSinglePrecision, platformid, deviceid);
	
	clo.buildCL();

	clo.initialize(tspan, x0, pars, sp); 
	
	// clo.seedRNG(mySeed);
	
	//warm up to pre-set nSteps and nPts
	clo.transient(); 
	
	start = std::chrono::high_resolution_clock::now();
	for(int i=0;i<nReps;++i){
		//~ clo.transient();
		clo.trajectory();
	}
	
	end = std::chrono::high_resolution_clock::now();
	elapsed_ms += end-start;
	
	//retrieve result from device
	std::vector<double> t=clo.getT();
	std::vector<double> x=clo.getX();
	std::vector<double> xf=clo.getX0();
	
	int trajIx=0;
	
	std::vector<int> nStored=clo.getNstored();
	std::cout<< "\nt \t xf:"<< "\n";
	for (int ix=0; ix<nStored[trajIx];++ix){
		std::cout << t[ix*nPts+trajIx] << "\t";
		
		for (int i=0; i<prob.nVar; ++i)
			std::cout << x[ix*nPts*prob.nVar+i*nPts+trajIx] << " ";
			
		std::cout<<std::endl;
	}
	
	std::cout<<std::endl;
    std::cout<< "Timepoints stored: " << nStored[trajIx] <<std::endl;
    std::cout<< "Compute time: " << elapsed_ms.count() << "ms" <<std::endl;
	
	
	} catch (std::exception &er) {
        std::cout<< "ERROR: " << er.what() << std::endl;
        std::cout<<"exiting...\n";
		return -1;
	}
    
	return 0;
}


//Generate random points within given bounds. Pack coordinates contiguously: all x1, then x2, etc.
template<typename T> std::vector<T> generateRandomPoints(std::vector<T> lb, std::vector<T> ub, int nPts)
{
	int dim=lb.size();
	std::vector<T> x(nPts*dim);
	
	T r;
	for (int i=0; i<dim; ++i)
	{	
		for (int j=0; j<nPts; ++j)
		{
			r = static_cast <T> (rand()) / static_cast <T> (RAND_MAX); //in [0,1]
			x[i*nPts+j]=lb[i] +r*(ub[i]-lb[i]); 
		}
	}
	return x;
}

//explicit instantiation of template function
template std::vector<float> generateRandomPoints(std::vector<float> lb, std::vector<float> ub, int nPts);
template std::vector<double> generateRandomPoints(std::vector<double> lb, std::vector<double> ub, int nPts);
