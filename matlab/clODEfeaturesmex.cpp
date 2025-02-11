// mex interface to clODE class
// (c) Patrick Fletcher 2017
//
// based on: 
// class_wrapper_template.cpp
// Example of using a C++ class via a MEX-file
// by Jonathan Chappelow (chappjc)
  
#include "mex.h"

#include <vector>
#include <memory> //shared_ptr
#include <map>
#include <sstream>

////////////////////////  BEGIN Step 1: Configuration  ////////////////////////

#include "OpenCLResource.hpp"
#include "CLODE.hpp"
#include "CLODEfeatures.hpp"
#include "clODEmexHelpers.hpp"

typedef CLODEfeatures class_type;

// List actions
enum class Action
{
    New,
    Delete,
    SetNewProblem,
    SetStepper,
    SetPrecision,
    SetOpenCL,
    BuildCL,
    Initialize, //overridden for clODEfeatures
    SetNPts,
    SetProblemData,
    SetTspan,
    SetX0,
    SetPars,
    SetSolverPars,
    SeedRNG,
    Transient,
    ShiftTspan,
    ShiftX0,
    GetTspan,
    GetX0,
    GetXf,
    GetStepperNames,
    GetProgramString,
    PrintStatus,
//from here are clODEfeatures derived actions
    SetObserverPars,
    SetObserver,
    InitializeObserver,
    Features,
    GetNFeatures,
    GetF,
    GetFeatureNames,
    GetObserverNames,
};

// Map string (first input argument to mexFunction) to an Action
const std::map<std::string, Action> actionTypeMap =
{
    { "new",            Action::New },
    { "delete",         Action::Delete },
    { "setnewproblem",  Action::SetNewProblem },
    { "setstepper",     Action::SetStepper },
    { "setprecision",   Action::SetPrecision },
    { "setopencl",      Action::SetOpenCL },
    { "buildcl",        Action::BuildCL},
    { "initialize",     Action::Initialize },
    { "setnpts",        Action::SetNPts },
    { "setproblemdata", Action::SetProblemData },
    { "settspan",       Action::SetTspan },
    { "setx0",          Action::SetX0 },
    { "setpars",        Action::SetPars },
    { "setsolverpars",  Action::SetSolverPars },
    { "seedrng",        Action::SeedRNG },
    { "transient",      Action::Transient },
    { "shifttspan",     Action::ShiftTspan },
    { "shiftx0",        Action::ShiftX0 },
    { "gettspan",       Action::GetTspan },
    { "getx0",          Action::GetX0 },
    { "getxf",          Action::GetXf },
    { "getsteppernames",        Action::GetStepperNames },
    { "getprogramstring",        Action::GetProgramString },
    { "printstatus",        Action::PrintStatus },
//from here are clODEfeatures derived actions
    { "setobserverpars", Action::SetObserverPars },
    { "setobserver",    Action::SetObserver },
    { "initobserver",    Action::InitializeObserver },
    { "features",       Action::Features },
    { "getnfeatures",   Action::GetNFeatures },
    { "getf",        	Action::GetF },
    { "getfeaturenames",        Action::GetFeatureNames },
    { "getobservernames",        Action::GetObserverNames },
}; 

//custom helper prototype
ObserverParams<cl_double> getMatlabOPstruct(const mxArray *opptr);

/////////////////////////  END Step 1: Configuration  /////////////////////////

typedef unsigned int handle_type;
typedef std::pair<handle_type, std::shared_ptr<class_type>> indPtrPair_type; // or boost::shared_ptr
typedef std::map<indPtrPair_type::first_type, indPtrPair_type::second_type> instanceMap_type;
typedef indPtrPair_type::second_type instPtr_t;

// getHandle pulls the integer handle out of prhs[1]
handle_type getHandle(int nrhs, const mxArray *prhs[]);
// checkHandle gets the position in the instance table
instanceMap_type::const_iterator checkHandle(const instanceMap_type&, handle_type);


void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {

    // static storage duration object for table mapping handles to instances
    static instanceMap_type instanceTab;

    if (nrhs < 1 || !mxIsChar(prhs[0]))
        mexErrMsgTxt("First input must be an action string ('new', 'delete', or a method name).");

    //~ char *actionCstr = mxArrayToString(prhs[0]); // convert char16_t to char
    //~ std::string actionStr(actionCstr); mxFree(actionCstr);
	std::string actionStr=getMatlabString(prhs[0]);

    for (auto & c : actionStr) c = ::tolower(c); // remove this for case sensitivity

    if (actionTypeMap.count(actionStr) == 0)
        mexErrMsgTxt(("Unrecognized action (not in actionTypeMap): " + actionStr).c_str());

    // If action is not "new" or "delete" try to locate an existing instance based on input handle
    instPtr_t instance;
    if (actionTypeMap.at(actionStr) != Action::New && actionTypeMap.at(actionStr) != Action::Delete) {
        handle_type h = getHandle(nrhs, prhs);
        instanceMap_type::const_iterator instIt = checkHandle(instanceTab, h);
        instance = instIt->second;
    }

    //NOTE: if not 'new', the first two RHS args are actionStr, instanceHandle. User inputs start at 3rd arg
    //TODO: could wrap this somehow to make the code here more readable...
    
	//////// Step 2: customize the each action in the switch in mexFuction ////////
    switch (actionTypeMap.at(actionStr))
    {
    case Action::New:
    {
		//sig: clODEobjective(nPar,nVar,nObjTimes,nObjVars,devicetype=all,vendor=any)
		
        handle_type newHandle = instanceTab.size() ? (instanceTab.rbegin())->first + 1 : 1;

		//create a new object
        std::pair<instanceMap_type::iterator, bool> insResult;
        
		//PARSE INPUT arguments ('new', problemInfoStruct, stepperInt, clSinglePrecisionBool, openclVendor=ANY, openclDeviceType=DEFAULT)
		
        if (nrhs < 4) {
			mexErrMsgTxt("Incorrect number of input arguments for clODEobjective object constructor");
		}
		
		ProblemInfo newProblem=getMatlabProblemStruct(prhs[1]);
        std::string stepper = mxArrayToString(prhs[2]);
		bool clSinglePrecision=(bool) mxGetScalar(prhs[3]);
        
        //opencl device selection: force matlab user to use "vendor" and/or "devicetype", always pass in args for this
		// cl_vendor vendor = static_cast<cl_vendor>((int)mxGetScalar(prhs[4]));
		// cl_deviceType devicetype = getDeviceTypeEnum(static_cast<int>(mxGetScalar(prhs[5])) );	
		// OpenCLResource opencl(devicetype,vendor);

        //opencl device selection: assume the matlab caller selects by plaformID and deviceID, as returned by queryOpenCL 
        unsigned int platformID = (unsigned int)mxGetScalar(prhs[4]); 
        unsigned int deviceID = (unsigned int)mxGetScalar(prhs[5]);
        std::string observer = mxArrayToString(prhs[6]);
	
		// OpenCLResource opencl(platformID, deviceID);
		// insResult = instanceTab.insert(indPtrPair_type(newHandle, std::make_shared<class_type>(newProblem,stepper,observer,clSinglePrecision, opencl)));
		insResult = instanceTab.insert(indPtrPair_type(newHandle, std::make_shared<class_type>(newProblem,stepper,observer,clSinglePrecision, platformID, deviceID)));

        if (!insResult.second) // sanity check
            mexPrintf("Oh, bad news.  Tried to add an existing handle."); // shouldn't ever happen
        else
            mexLock(); // add to the lock count

		// return the handle
        plhs[0] = mxCreateDoubleScalar(insResult.first->first); // == newHandle

        break;
    }
    case Action::Delete:
    { //rhs='delete',instanceID
        instanceMap_type::const_iterator instIt = checkHandle(instanceTab, getHandle(nrhs, prhs));
        instanceTab.erase(instIt);
        mexUnlock();
        plhs[0] = mxCreateLogicalScalar(instanceTab.empty()); // info
        break;
    }
    //base class CLODE methods (rhs 0='methodName', 1=instanceID, ...)
    case Action::SetNewProblem:
	{ //inputs: prob
        instance->setNewProblem(getMatlabProblemStruct(prhs[2]));
        break;
	}
    case Action::SetStepper:
	{ //inputs: stepper name
        std::string stepper = mxArrayToString(prhs[2]);
        instance->setStepper(stepper);
        break;
	}
    case Action::SetPrecision:
	{ //inputs: clSinglePrecision
        instance->setPrecision((bool) mxGetScalar(prhs[2]));
        break;
	}
    case Action::SetOpenCL:
	{ //inputs: vendor/devicetype
		unsigned int platformID = static_cast<unsigned int>(mxGetScalar(prhs[2]));
		unsigned int deviceID = static_cast<unsigned int>(mxGetScalar(prhs[3]));
        instance->setOpenCL(platformID, deviceID);
        break;
	}
    case Action::Initialize:
	{ //inputs: tspan, x0, pars, sp, op //OVERRIDE
        std::vector<cl_double> tspan ( static_cast<cl_double *>(mxGetData(prhs[2])),  static_cast<cl_double *>(mxGetData(prhs[2])) + mxGetNumberOfElements(prhs[2]) ); 
        std::vector<cl_double> x0 ( static_cast<cl_double *>(mxGetData(prhs[3])),  static_cast<cl_double *>(mxGetData(prhs[3])) + mxGetNumberOfElements(prhs[3]) ); 
        std::vector<cl_double> pars (static_cast<cl_double *>(mxGetData(prhs[4])),  static_cast<cl_double *>(mxGetData(prhs[4])) + mxGetNumberOfElements(prhs[4]) );  
		SolverParams<cl_double> sp = getMatlabSPstruct(prhs[5]);
		ObserverParams<cl_double> op = getMatlabOPstruct(prhs[6]);
        instance->initialize(tspan, x0, pars, sp, op);       
        break;
	}
    case Action::BuildCL:
	{ //inputs: none
        #if defined(WIN32)||defined(_WIN64)
            _putenv_s("CUDA_CACHE_DISABLE", "1");
        #else
            setenv("CUDA_CACHE_DISABLE", "1", 1);
        #endif
        instance->buildCL();
        break;
	}
    case Action::SetNPts:
	{ //inputs: newNpts
        instance->setNpts((cl_int)mxGetScalar(prhs[2]));
        break;
	}
    case Action::SetProblemData:
	{ //inputs: x0, pars
        std::vector<cl_double> x0( static_cast<cl_double *>(mxGetData(prhs[2])),  static_cast<cl_double *>(mxGetData(prhs[2])) + mxGetNumberOfElements(prhs[2]) ); 
        std::vector<cl_double> pars(static_cast<cl_double *>(mxGetData(prhs[3])),  static_cast<cl_double *>(mxGetData(prhs[3])) + mxGetNumberOfElements(prhs[3]) );  		
        instance->setProblemData(x0, pars);
        break;
	}
    case Action::SetTspan:
	{ //inputs: tspan
        std::vector<cl_double> tspan ( static_cast<cl_double *>(mxGetData(prhs[2])),  static_cast<cl_double *>(mxGetData(prhs[2])) + mxGetNumberOfElements(prhs[2]) ); 
        instance->setTspan(tspan);
        break;
	}
    case Action::SetX0:
	{ //inputs: x0
        std::vector<cl_double> x0( static_cast<cl_double *>(mxGetData(prhs[2])),  static_cast<cl_double *>(mxGetData(prhs[2])) + mxGetNumberOfElements(prhs[2]) );  
        instance->setX0(x0);
        break;
	}
    case Action::SetPars:
	{ //inputs: pars
        std::vector<cl_double> pars( static_cast<cl_double *>(mxGetData(prhs[2])),  static_cast<cl_double *>(mxGetData(prhs[2])) + mxGetNumberOfElements(prhs[2]) );  
        instance->setPars(pars);
        break;
	}
    case Action::SetSolverPars:
	{	//inputs: sp 
		SolverParams<cl_double> sp = getMatlabSPstruct(prhs[2]);
        instance->setSolverParams(sp);
        break;
	}
    case Action::SeedRNG:
	{ //inputs: none, or mySeedInt
		if (nrhs==2) 
			instance->seedRNG();
		else if (nrhs==3)
			instance->seedRNG((cl_int)mxGetScalar(prhs[2]));

        break;
	}
    case Action::Transient:
	{
        instance->transient();
        break;
	}
    case Action::ShiftTspan:
	{
        instance->shiftTspan();

        break;
	}
    case Action::ShiftX0:
	{
        instance->shiftX0();

        break;
	}
    case Action::GetTspan:
    {
        std::vector<cl_double> tspan=instance->getTspan();
		plhs[0]=mxCreateDoubleMatrix(tspan.size(), 1, mxREAL);
        std::copy(tspan.begin(), tspan.end(), (cl_double *)mxGetData(plhs[0]));
        break;
	}
    case Action::GetX0:
    {
        std::vector<cl_double> x0=instance->getX0();
		plhs[0]=mxCreateDoubleMatrix(x0.size(), 1, mxREAL);
        std::copy(x0.begin(), x0.end(), (cl_double *)mxGetData(plhs[0]));
        break;
	}
    case Action::GetXf:
    {
        std::vector<cl_double> xf=instance->getXf();
		plhs[0]=mxCreateDoubleMatrix(1, xf.size(), mxREAL);
        std::copy(xf.begin(), xf.end(), (cl_double *)mxGetData(plhs[0]));
        break;
	}
    case Action::GetStepperNames:
    {
        std::vector<std::string> names=instance->getAvailableSteppers();
		plhs[0]=mxCreateCellMatrix(names.size(), 1);    
        for (mwIndex i=0; i<names.size(); i++)
            mxSetCell(plhs[0], i, mxCreateString(names[i].c_str()));
        break;
    }
    case Action::GetProgramString:
    {
		plhs[0]=mxCreateCellMatrix(1, 1);    
        mxSetCell(plhs[0], 0, mxCreateString(instance->getProgramString().c_str()));
        break;
    }
    case Action::PrintStatus:
    {   
        instance->printStatus();
        break;
    }
	//CLODEfeatures methods:
    case Action::SetObserverPars:
	{	//inputs: op 
		ObserverParams<cl_double> op = getMatlabOPstruct(prhs[2]);
        instance->setObserverParams(op);
        break;
	}
    case Action::SetObserver:
    {   //input string with observer name
        std::string observer = mxArrayToString(prhs[2]);
        instance->setObserver(observer);
        break;
	}
    case Action::InitializeObserver:
    {   //no inputs
        instance->initializeObserver();
        break;
	}
    case Action::Features:
    {   //inputs: none, or int - 1=do init, 0=don't init
		if (nrhs==2) { 
			instance->features(); }
		else if (nrhs==3) {
			bool doInit=(bool)mxGetScalar(prhs[2]);
			if (doInit!=0 && doInit!=1) {mexErrMsgTxt("Argument must be 0 or 1 for features with observer data initialization control");}
			instance->features(doInit);
		}
        break;
	}
    case Action::GetNFeatures:
    {  
		plhs[0]=mxCreateDoubleScalar(instance->getNFeatures());
        break;
	}
    case Action::GetF:
    {  
        std::vector<cl_double> F=instance->getF();
		plhs[0]=mxCreateDoubleMatrix(F.size(), 1, mxREAL);
        std::copy(F.begin(), F.end(), (cl_double *)mxGetData(plhs[0]));
        break;
	}
    case Action::GetFeatureNames:
    {
        std::vector<std::string> names=instance->getFeatureNames();
		plhs[0]=mxCreateCellMatrix(names.size(), 1);    
        for (mwIndex i=0; i<names.size(); i++)
            mxSetCell(plhs[0], i, mxCreateString(names[i].c_str()));
        break;
    }
    case Action::GetObserverNames:
    {
        std::vector<std::string> names=instance->getAvailableObservers();
		plhs[0]=mxCreateCellMatrix(names.size(), 1);    
        for (mwIndex i=0; i<names.size(); i++)
            mxSetCell(plhs[0], i, mxCreateString(names[i].c_str()));
        break;
    }
    default:
        mexErrMsgTxt(("Unhandled action: " + actionStr).c_str());
        break;
    }
    ////////////////////////////////  DONE!  ////////////////////////////////
}

handle_type getHandle(int nrhs, const mxArray *prhs[])
{
    if (nrhs < 2 || mxGetNumberOfElements(prhs[1]) != 1) // mxIsScalar in R2015a+
        mexErrMsgTxt("Specify an instance with an integer handle.");
    return static_cast<handle_type>(mxGetScalar(prhs[1]));
}

instanceMap_type::const_iterator checkHandle(const instanceMap_type& m, handle_type h)
{
    auto it = m.find(h);

    if (it == m.end()) {
        std::stringstream ss; ss << "No instance corresponding to handle " << h << " found.";
        mexErrMsgTxt(ss.str().c_str());
    }

    return it;
}

//This struct is defined in observers.cl, included via clODEfeatures.hpp  <----maybe needs fixing...
ObserverParams<cl_double> getMatlabOPstruct(const mxArray *opptr) {
	ObserverParams<cl_double> op;
	op.eVarIx=(cl_int)mxGetScalar( mxGetField(opptr,0,"eVarIx") ) -1; //convert matlab 1-based index to C 0-based.
	op.fVarIx=(cl_int)mxGetScalar( mxGetField(opptr,0,"fVarIx") ) -1;
	op.maxEventCount=(cl_int)mxGetScalar( mxGetField(opptr,0,"maxEventCount") );
	op.minXamp=mxGetScalar( mxGetField(opptr,0,"minXamp") );
	op.minIMI=mxGetScalar( mxGetField(opptr,0,"minIMI") );
	op.nHoodRadius=mxGetScalar( mxGetField(opptr,0,"nHoodRadius") );
	op.xUpThresh=mxGetScalar( mxGetField(opptr,0,"xUpThresh") );
	op.xDownThresh=mxGetScalar( mxGetField(opptr,0,"xDownThresh") );
	op.dxUpThresh=mxGetScalar( mxGetField(opptr,0,"dxUpThresh") );
	op.dxDownThresh=mxGetScalar( mxGetField(opptr,0,"dxDownThresh") );
	op.eps_dx=mxGetScalar( mxGetField(opptr,0,"eps_dx") );
	return op;
}
