/*
 * OpenCLResource.cpp
 *
 *  Copyright 2017 Patrick Fletcher <patrick.fletcher@nih.gov>
 * 
 */

#include "OpenCLResource.hpp"

// #define __CL_ENABLE_EXCEPTIONS
// #if defined(__APPLE__) || defined(__MACOSX)
//     #include "OpenCL/cl.hpp"
// #else
//     #include <CL/cl.hpp>
// #endif

//if compiling from matlab MEX, redefine printf to mexPrintf so it prints to matlab command window.
// #define dbg_printf printf
#define dbg_printf
#ifdef MATLAB_MEX_FILE
#include "mex.h"
#define printf mexPrintf
#endif

#include <stdexcept>
#include <fstream>
#include <stdio.h>

/******************************** 
 * OpenCLResource Member Functions
 ********************************/

OpenCLResource::OpenCLResource()
{
    getPlatformAndDevices(CL_DEVICE_TYPE_DEFAULT, VENDOR_ANY);
    initializeOpenCL();
}

OpenCLResource::OpenCLResource(cl_deviceType type)
{

    getPlatformAndDevices(type, VENDOR_ANY);
    initializeOpenCL();
}

OpenCLResource::OpenCLResource(cl_vendor vendor)
{

    getPlatformAndDevices(CL_DEVICE_TYPE_DEFAULT, vendor);
    initializeOpenCL();
}

OpenCLResource::OpenCLResource(cl_deviceType type, cl_vendor vendor)
{

    getPlatformAndDevices(type, vendor);
    initializeOpenCL();
}

OpenCLResource::OpenCLResource(int argc, char **argv)
{

    //modified from openCLUtilities to include accelerators as a devicetype
    cl_deviceType type = CL_DEVICE_TYPE_ALL;
    cl_vendor vendor = VENDOR_ANY;
    int nValidArgs = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--device") == 0)
        {
            if (strcmp(argv[i + 1], "cpu") == 0)
            {
                type = CL_DEVICE_TYPE_CPU;
            }
            else if (strcmp(argv[i + 1], "gpu") == 0)
            {
                type = CL_DEVICE_TYPE_GPU;
            }
            else if (strcmp(argv[i + 1], "accel") == 0)
            {
                type = CL_DEVICE_TYPE_ACCELERATOR;
            }
            else
                throw cl::Error(1, "Unkown device type used with --device");
            i++;
            nValidArgs++;
        }
        else if (strcmp(argv[i], "--vendor") == 0)
        {
            if (strcmp(argv[i + 1], "amd") == 0)
            {
                vendor = VENDOR_AMD;
            }
            else if (strcmp(argv[i + 1], "intel") == 0)
            {
                vendor = VENDOR_INTEL;
            }
            else if (strcmp(argv[i + 1], "nvidia") == 0)
            {
                vendor = VENDOR_NVIDIA;
            }
            else
                throw cl::Error(1, "Unkown vendor name used with --vendor");
            i++;
            nValidArgs++;
        }
    }

    if (nValidArgs == 0 && argc > 1)
    {
        printf("Warning: OpenCLResource didn't recognize the command line arguments. Using default device. \n");
    }

    getPlatformAndDevices(type, vendor);
    initializeOpenCL();
}

OpenCLResource::OpenCLResource(unsigned int platformID, unsigned int deviceID)
{

    std::vector<unsigned int> deviceIDs(1, deviceID);
    getPlatformAndDevices(platformID, deviceIDs);
    initializeOpenCL();
}

OpenCLResource::OpenCLResource(unsigned int platformID, std::vector<unsigned int> deviceIDs)
{

    getPlatformAndDevices(platformID, deviceIDs);
    initializeOpenCL();
};

//modified from openCLUtilities
//TODO: check various possibilities on default? eg: any Accel, any non-intel GPU, intel GPU, any CPU
void OpenCLResource::getPlatformAndDevices(cl_deviceType type, cl_vendor vendor)
{

    //query all platform and device info, store as a vector of structs
    //~ std::vector<platformInfo> pinfo=queryOpenCL();

    // Get available platforms
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);

    if (platforms.size() == 0)
        throw cl::Error(1, "No OpenCL platforms were found");

    int tempID = -1;
    if (vendor != VENDOR_ANY)
    {
        std::string vendorStr;
        switch (vendor)
        {
        case VENDOR_NVIDIA:
            vendorStr = "NVIDIA";
            break;
        case VENDOR_AMD:
            vendorStr = "Advanced Micro Devices";
            break;
        case VENDOR_INTEL:
            vendorStr = "Intel";
            break;
        default:
            throw cl::Error(1, "Invalid vendor specified");
            break;
        }

        std::vector<cl::Platform> tempPlatforms;
        for (unsigned int i = 0; i < platforms.size(); i++)
        {
            if (platforms[i].getInfo<CL_PLATFORM_VENDOR>().find(vendorStr) != std::string::npos)
            {
                tempPlatforms.push_back(platforms[i]);
            }
        }

        platforms = tempPlatforms; //keep only the platforms with correct vendor
    }

    std::vector<cl::Device> tempDevices;
    for (unsigned int i = 0; i < platforms.size(); i++)
    {
        try
        {
            platforms[i].getDevices(type, &tempDevices);
            //TODO: apply extra device filters (eg. extensionSupported,enoughMem,...) here?
            tempID = i;
            break;
        }
        catch (cl::Error &e)
        {
            continue;
        }
    }

    if (tempID == -1)
        throw cl::Error(1, "No compatible OpenCL platform found");

    //we found a platform with compatible device(s) to use
    platform = platforms[tempID];
    devices = tempDevices;

    //get info for the selected plaform and device(s)
    platform_info = getPlatformInfo(platform, devices);
}

void OpenCLResource::getPlatformAndDevices(unsigned int platformID, std::vector<unsigned int> deviceIDs)
{

    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    if (platforms.size() == 0)
        throw cl::Error(1, "No OpenCL platforms were found");

    if (platformID < platforms.size())
    {
        platform = platforms[platformID];
    }
    else
    {
        throw std::out_of_range("Specified platformID exceeds number of available platforms");
    }

    std::vector<cl::Device> tempDevices;
    platform.getDevices(CL_DEVICE_TYPE_ALL, &tempDevices);
    for (unsigned int i = 0; i < deviceIDs.size(); ++i)
    {

        if (deviceIDs[i] < tempDevices.size())
        {
            devices.push_back(tempDevices[deviceIDs[i]]);
        }
        else
        {
            throw std::out_of_range("Specified deviceID exceeds the number devices on the selected platform");
        }
    }

    //get info for the selected plaform and device(s)
    platform_info = getPlatformInfo(platform, devices);
}

//create the OpenCL context from the device list, and a queue for each device
void OpenCLResource::initializeOpenCL()
{

    try
    {
        context = cl::Context(devices);
        for (unsigned int i = 0; i < devices.size(); ++i)
            queues.push_back(cl::CommandQueue(context, devices[i]));
    }
    catch (cl::Error &er)
    {
        printf("ERROR: %s(%s)\n", er.what(), CLErrorString(er.err()).c_str());
        throw er;
    }

    //~ printf("OpenCLResource Created\n");
}

//attempt to build OpenCL program given as a string, build options empty if not supplied.
void OpenCLResource::buildProgramFromString(std::string sourceStr, std::string buildOptions)
{

    cl::Program::Sources source(1, std::make_pair(sourceStr.c_str(), sourceStr.length()));
    std::string buildLog;
    cl_int builderror;
    try
    {
        program = cl::Program(context, source, &error);
        // printf("Program Object creation error code: %s\n",CLErrorString(error).c_str());

        builderror = program.build(devices, buildOptions.c_str());
        // printf("Program Object build error code: %s\n",CLErrorString(builderror).c_str());

        // std::string kernelnames;
        // program.getInfo(CL_PROGRAM_KERNEL_NAMES,&kernelnames);
        // printf("Kernels built:   %s\n", kernelnames.c_str());
    }
    catch (cl::Error &er)
    {
        printf("ERROR: %s(%s)\n", er.what(), CLErrorString(er.err()).c_str());
        if (er.err() == CL_BUILD_PROGRAM_FAILURE)
        {
            // printf("%s\n",sourceStr.c_str());
            for (unsigned int i = 0; i < devices.size(); ++i)
            {
                program.getBuildInfo(devices[i], CL_PROGRAM_BUILD_LOG, &buildLog);
                printf("OpenCL build log, Device %u:\n", i);
                printf("%s\n", buildLog.c_str());
            }
        }
        throw er;
    }
}

void OpenCLResource::buildProgramFromSource(std::string filename, std::string buildOptions)
{

    std::string sourceStr = read_file(filename);
    buildProgramFromString(sourceStr, buildOptions);
}

//prints the selected platform and devices information (queried on the fly)
void OpenCLResource::print()
{
    std::string tmp;
    printf("\nSelected platform and device: \n");
    printf("\nPlatform  --------------------\n");
    printPlatformInfo(platform_info);
}

/******************************** 
 * Other functions
 ********************************/

//get info of all devices on all platforms
std::vector<platformInfo> queryOpenCL()
{

    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    if (platforms.size() == 0)
        throw cl::Error(1, "No OpenCL platforms were found");

    std::vector<platformInfo> pinfo;

    for (unsigned int i = 0; i < platforms.size(); ++i)
    {
        pinfo.push_back(getPlatformInfo(platforms[i]));
    }

    return pinfo;
}

//get info of a given cl::Platform and its devices (optionally only devices of a given type, default is CL_DEVICE_TYPE_ALL)
platformInfo getPlatformInfo(cl::Platform platform, std::vector<cl::Device> devices)
{

    platformInfo pinfo;
    platform.getInfo(CL_PLATFORM_NAME, &pinfo.name);
    platform.getInfo(CL_PLATFORM_VENDOR, &pinfo.vendor);
    platform.getInfo(CL_PLATFORM_VERSION, &pinfo.version);

    if (devices.size() == 0)
    { //get all the devices
        platform.getDevices(CL_DEVICE_TYPE_ALL, &devices);
    }

    pinfo.nDevices = (unsigned int)devices.size();

    for (unsigned int j = 0; j < pinfo.nDevices; j++)
        pinfo.device_info.push_back(getDeviceInfo(devices[j]));

    return pinfo;
}

//get info for given cl::Device
deviceInfo getDeviceInfo(cl::Device device)
{

    deviceInfo dinfo;

    //Get device info for this compute resource
    device.getInfo(CL_DEVICE_NAME, &dinfo.name);
    device.getInfo(CL_DEVICE_VENDOR, &dinfo.vendor);
    device.getInfo(CL_DEVICE_VERSION, &dinfo.version);
    device.getInfo(CL_DEVICE_TYPE, &dinfo.devType);
    switch (dinfo.devType)
    {
    case CL_DEVICE_TYPE_CPU:
        dinfo.devTypeStr = "CPU";
        break;
    case CL_DEVICE_TYPE_GPU:
        dinfo.devTypeStr = "GPU";
        break;
    case CL_DEVICE_TYPE_ACCELERATOR:
        dinfo.devTypeStr = "Accelerator";
        break;
    default:
        dinfo.devTypeStr = "Unknown";
    }

    device.getInfo(CL_DEVICE_MAX_COMPUTE_UNITS, &dinfo.computeUnits);
    device.getInfo(CL_DEVICE_MAX_CLOCK_FREQUENCY, &dinfo.maxClock);
    device.getInfo(CL_DEVICE_MAX_WORK_GROUP_SIZE, &dinfo.maxWorkGroupSize);
    device.getInfo(CL_DEVICE_GLOBAL_MEM_SIZE, &dinfo.deviceMemSize);
    device.getInfo(CL_DEVICE_MAX_MEM_ALLOC_SIZE, &dinfo.maxMemAllocSize);
    device.getInfo(CL_DEVICE_EXTENSIONS, &dinfo.extensions);

    std::string doubleStr = "fp64";
    dinfo.doubleSupport = dinfo.extensions.find(doubleStr) != std::string::npos;

    device.getInfo(CL_DEVICE_EXTENSIONS, &dinfo.extensions);
    device.getInfo(CL_DEVICE_AVAILABLE, &dinfo.deviceAvailable);

    return dinfo;
}

//print information about all platforms and devices found
void printOpenCL()
{

    printf("\nQuerying OpenCL platforms...\n");
    std::vector<platformInfo> pinfo = queryOpenCL();
    printOpenCL(pinfo);
}

//print information about all platforms and devices found, given pre-queried array of platformInfo structs
void printOpenCL(std::vector<platformInfo> pinfo)
{

    printf("Number of platforms found: %u\n", (unsigned int)pinfo.size());
    for (unsigned int i = 0; i < pinfo.size(); ++i)
    {
        printf("\nPlatform %d. ------------------------------\n", i);
        printPlatformInfo(pinfo[i]);
    }
    printf("\n");
}

//print information about a platform and its devices given pre-queried info in platformInfo struct
void printPlatformInfo(platformInfo pinfo)
{
    printf("Name:    %s\n", pinfo.name.c_str());
    printf("Vendor:  %s\n", pinfo.vendor.c_str());
    printf("Version: %s\n", pinfo.version.c_str());

    for (unsigned int j = 0; j < pinfo.nDevices; j++)
    {
        printf("\nDevice %d. --------------------\n", j);
        printDeviceInfo(pinfo.device_info[j]);
    }
}

//print info about a specific cl::Device
void printDeviceInfo(cl::Device device)
{
    deviceInfo dinfo = getDeviceInfo(device);
    printDeviceInfo(dinfo);
}

//print info about a specific cl::Device given pre-queried info in deviceInfo struct
void printDeviceInfo(deviceInfo dinfo)
{
    printf("Name:   %s\n", dinfo.name.c_str());
    printf("Type:   %s\n", dinfo.devTypeStr.c_str());
    printf("Vendor: %s\n", dinfo.vendor.c_str());
    printf("Version: %s\n", dinfo.version.c_str());
    printf("Compute units (CUs): %d\n", dinfo.computeUnits);
    printf("Clock frequency:     %d MHz\n", dinfo.maxClock);
    printf("Global memory size:  %llu MB\n", (long long unsigned int)(dinfo.deviceMemSize / 1024 / 1024));
    printf("Max allocation size: %llu MB\n", (long long unsigned int)(dinfo.maxMemAllocSize / 1024 / 1024));
    printf("Max work group/CU:   %d\n", (int)dinfo.maxWorkGroupSize);
    printf("Double support:      %s\n", (dinfo.doubleSupport ? "true" : "false"));
    printf("Device available:    %s\n", (dinfo.deviceAvailable ? "true" : "false"));
};

std::string CLErrorString(cl_int error)
{
    switch (error)
    {
    case CL_SUCCESS:
        return std::string("Success!");
    case CL_DEVICE_NOT_FOUND:
        return std::string("Device not found.");
    case CL_DEVICE_NOT_AVAILABLE:
        return std::string("Device not available");
    case CL_COMPILER_NOT_AVAILABLE:
        return std::string("Compiler not available");
    case CL_MEM_OBJECT_ALLOCATION_FAILURE:
        return std::string("Memory object allocation failure");
    case CL_OUT_OF_RESOURCES:
        return std::string("Out of resources");
    case CL_OUT_OF_HOST_MEMORY:
        return std::string("Out of host memory");
    case CL_PROFILING_INFO_NOT_AVAILABLE:
        return std::string("Profiling information not available");
    case CL_MEM_COPY_OVERLAP:
        return std::string("Memory copy overlap");
    case CL_IMAGE_FORMAT_MISMATCH:
        return std::string("Image format mismatch");
    case CL_IMAGE_FORMAT_NOT_SUPPORTED:
        return std::string("Image format not supported");
    case CL_BUILD_PROGRAM_FAILURE:
        return std::string("Program build failure");
    case CL_MAP_FAILURE:
        return std::string("Map failure");
    case CL_INVALID_VALUE:
        return std::string("Invalid value");
    case CL_INVALID_DEVICE_TYPE:
        return std::string("Invalid device type");
    case CL_INVALID_PLATFORM:
        return std::string("Invalid platform");
    case CL_INVALID_DEVICE:
        return std::string("Invalid device");
    case CL_INVALID_CONTEXT:
        return std::string("Invalid context");
    case CL_INVALID_QUEUE_PROPERTIES:
        return std::string("Invalid queue properties");
    case CL_INVALID_COMMAND_QUEUE:
        return std::string("Invalid command queue");
    case CL_INVALID_HOST_PTR:
        return std::string("Invalid host pointer");
    case CL_INVALID_MEM_OBJECT:
        return std::string("Invalid memory object");
    case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR:
        return std::string("Invalid image format descriptor");
    case CL_INVALID_IMAGE_SIZE:
        return std::string("Invalid image size");
    case CL_INVALID_SAMPLER:
        return std::string("Invalid sampler");
    case CL_INVALID_BINARY:
        return std::string("Invalid binary");
    case CL_INVALID_BUILD_OPTIONS:
        return std::string("Invalid build options");
    case CL_INVALID_PROGRAM:
        return std::string("Invalid program");
    case CL_INVALID_PROGRAM_EXECUTABLE:
        return std::string("Invalid program executable");
    case CL_INVALID_KERNEL_NAME:
        return std::string("Invalid kernel name");
    case CL_INVALID_KERNEL_DEFINITION:
        return std::string("Invalid kernel definition");
    case CL_INVALID_KERNEL:
        return std::string("Invalid kernel");
    case CL_INVALID_ARG_INDEX:
        return std::string("Invalid argument index");
    case CL_INVALID_ARG_VALUE:
        return std::string("Invalid argument value");
    case CL_INVALID_ARG_SIZE:
        return std::string("Invalid argument size");
    case CL_INVALID_KERNEL_ARGS:
        return std::string("Invalid kernel arguments");
    case CL_INVALID_WORK_DIMENSION:
        return std::string("Invalid work dimension");
    case CL_INVALID_WORK_GROUP_SIZE:
        return std::string("Invalid work group size");
    case CL_INVALID_WORK_ITEM_SIZE:
        return std::string("Invalid work item size");
    case CL_INVALID_GLOBAL_OFFSET:
        return std::string("Invalid global offset");
    case CL_INVALID_EVENT_WAIT_LIST:
        return std::string("Invalid event wait list");
    case CL_INVALID_EVENT:
        return std::string("Invalid event");
    case CL_INVALID_OPERATION:
        return std::string("Invalid operation");
    case CL_INVALID_GL_OBJECT:
        return std::string("Invalid OpenGL object");
    case CL_INVALID_BUFFER_SIZE:
        return std::string("Invalid buffer size");
    case CL_INVALID_MIP_LEVEL:
        return std::string("Invalid mip-map level");
    default:
        return std::string("Unknown");
    }
}

// Read source file
std::string read_file(std::string filename)
{
    std::ifstream sourceFile(filename.c_str());
    if (sourceFile.fail())
        throw cl::Error(1, "Failed to open OpenCL source file");
    std::string sourceStr(std::istreambuf_iterator<char>(sourceFile), (std::istreambuf_iterator<char>())); //second arg is "end of stream" iterator
    sourceFile.close();
    // printf("%s",sourceStr.c_str());
    return sourceStr;
}
