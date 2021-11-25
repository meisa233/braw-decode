#include "braw.h"


void FrameProcessor::ReadComplete(IBlackmagicRawJob* readJob, HRESULT result,
IBlackmagicRawFrame* frame)
{
	BrawInfo* info = nullptr;
	readJob->GetUserData((void**)&info);
if (result == S_OK)
	frame->SetResourceFormat(info->resourceFormat);
if (result == S_OK)
	frame->SetResolutionScale(info->resolutionScale);
	
	
	IBlackmagicRawJob* decodeAndProcessJob = nullptr;
if (result == S_OK)
	frame->CreateJobDecodeAndProcessFrame(nullptr, nullptr, &decodeAndProcessJob);
if (result == S_OK)
	decodeAndProcessJob->SetUserData(info);
if (result == S_OK)
	result = decodeAndProcessJob->Submit();

	if (result != S_OK)
	{
		if (decodeAndProcessJob)
			decodeAndProcessJob->Release();

    delete info;
	}

	readJob->Release();
}

void FrameProcessor::ProcessComplete(IBlackmagicRawJob* job, HRESULT result,
IBlackmagicRawProcessedImage* processedImage)
{
	BrawInfo* info = nullptr;
	job->GetUserData((void**)&info);

	if(info->verbose)
		std::cerr << "\rBRAW Frame/threads [" << info->frameIndex << "][" << info->threads << "] ";

	if (info->infoPass)
	{
		processedImage->GetWidth(&info->width);
		processedImage->GetHeight(&info->height);
	}
	
	unsigned int size = 0;
	void* imageData = nullptr;
	processedImage->GetResource(&imageData);
	processedImage->GetResourceSizeBytes(&size);

	// Print out image data
	if (!info->infoPass)
		std::cout.write(reinterpret_cast<char*>(imageData), size); 

	job->Release();

	if (!info->infoPass)
		info->threads--;
}

Braw::Braw()
{
	info = new BrawInfo();
}

Braw::~Braw()
{
	codec->FlushJobs();

	if (clip != nullptr)
		clip->Release();

	if (codec != nullptr)
		codec->Release();

	if (factory != nullptr)
		factory->Release();
}

void Braw::openFile(std::string filepath)
{

	// Store file
	info->filename = filepath;

	// Setup BRAW SDK
	factory = CreateBlackmagicRawFactoryInstanceFromPath(lib);
  HRESULT result;
  if (factory == nullptr)
  {
    std::cerr << "Failed to create IBlackmagicRawFactory!" << std::endl;
    result = E_FAIL;
  }
  result = S_OK;
  result = factory->CreateCodec(&codec);
  if (result != S_OK)
  {
    std::cerr << "Failed to create IBlackmagicRaw!" << std::endl;
  }
	const char *cc = info->filename.c_str();
  CFStringRef c = CFStringCreateWithCString(NULL, cc, kCFStringEncodingUTF8);

  result = codec->OpenClip(c, &clip);
  if (result != S_OK)
  {
    std::cerr << "Failed to open IBlackmagicRawClip!" << std::endl;
  }
	codec->SetCallback(&frameProcessor);
	std::cerr <<"info->frameCount" <<info->frameCount;
	uint64_t * frameCount = (uint64_t *)(info->frameCount);
	//clip->GetFrameCount(&(info->frameCount));
	clip->GetFrameCount(frameCount);
	clip->GetFrameRate(&info->framerate);

	if(frameOut == 0)
		frameOut = info->frameCount;

	IBlackmagicRawJob* jobRead = nullptr;
	clip->CreateJobReadFrame(info->frameIndex, &jobRead);
	jobRead->SetUserData(info);
	result = jobRead->Submit();

        if (result != S_OK)
        {
            if (jobRead != nullptr)
                jobRead->Release();
        }
	codec->FlushJobs();

	info->infoPass = false;

	if (clipInfo)
	{
		printInfo();
		return;
	}else if (ffPrint)
	{
		printFFFormat();
		return;
	}else{
		decode();
	}
}

void Braw::decode()
{
	info->frameIndex = frameIn;

	if(info->verbose)
		std::cerr << "Begining decode of " << frameIn << "-" << frameOut << std::endl;
	std::cerr << "frameOut is" << frameOut << std::endl;
	while (info->frameIndex < frameOut)
	{
		if (info->threads >= maxThreads)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(50));
			continue;
		}

		IBlackmagicRawJob* jobRead = nullptr;
		clip->CreateJobReadFrame(info->frameIndex, &jobRead);
		jobRead->SetUserData(info);
		HRESULT result;
		result = jobRead->Submit();

		if (result != S_OK)
		{
		    if (jobRead != nullptr)
			jobRead->Release();
		}

		info->threads++;
		info->frameIndex++;
	}
}

void Braw::addArgs(ArgParse *parser)
{	
	parser->addArg(argColorFormat,&rawColorFormat,argColorFormatOptions,argColorFormatDescriptions);
	parser->addArg(argMaxThreads,&rawMaxThreads);
	parser->addArg(argFrameIn,&rawFrameIn);
	parser->addArg(argFrameOut,&rawFrameOut);
	parser->addArg(argScale,&rawScale,argScaleOptions);
	parser->addArg(argClipInfo,&clipInfo);
	parser->addArg(argVerbose,&info->verbose);
	parser->addArg(argFFFormat,&ffPrint);
}

void Braw::validateArgs()
{
	try
	{
		maxThreads = std::stoi(rawMaxThreads);
		frameIn = std::stoi(rawFrameIn);
		frameOut = std::stoi(rawFrameOut);
		info->scale = std::stoi(rawScale);

		// Get color format
		info->resourceFormat = rawColorFormat == "rgba" ? blackmagicRawResourceFormatRGBAU8 : info->resourceFormat;
		info->resourceFormat = rawColorFormat == "bgra" ? blackmagicRawResourceFormatBGRAU8 : info->resourceFormat;
		info->resourceFormat = rawColorFormat == "16il" ? blackmagicRawResourceFormatRGBU16 : info->resourceFormat;
		info->resourceFormat = rawColorFormat == "16pl" ? blackmagicRawResourceFormatRGBU16Planar : info->resourceFormat;
		info->resourceFormat = rawColorFormat == "f32s" ? blackmagicRawResourceFormatRGBF32 : info->resourceFormat;
		info->resourceFormat = rawColorFormat == "f32p" ? blackmagicRawResourceFormatRGBF32Planar : info->resourceFormat;
		info->resourceFormat = rawColorFormat == "f32a" ? blackmagicRawResourceFormatBGRAF32 : info->resourceFormat;
		// Get scale		
		info->resolutionScale = rawScale == "1" ? blackmagicRawResolutionScaleFull : info->resolutionScale;
		info->resolutionScale = rawScale == "2" ? blackmagicRawResolutionScaleHalf : info->resolutionScale;
		info->resolutionScale = rawScale == "4" ? blackmagicRawResolutionScaleQuarter : info->resolutionScale;
		info->resolutionScale = rawScale == "8" ? blackmagicRawResolutionScaleEighth : info->resolutionScale;
	} catch (const std::exception& e) {
		std::cerr << "Invalid BRAW parameters" << std::endl;
		std::exit(1);
	}
}

void Braw::printInfo()
{
	std::cout << "Braw Decoder: Alpha" << std::endl;
	std::cout << "File: " << info->filename << std::endl;
	std::cout << "Resolution: " << info->width << "x" << info->height << std::endl;
	std::cout << "Framerate: " << info->framerate << std::endl;
	std::cout << "Frame Count: " << info->frameCount << std::endl;
	std::cout << "Scale: " << info->scale << std::endl;
}

void Braw::printFFFormat()
{
	std::string ffmpegInputFormat = "-f rawvideo -pixel_format ";
	ffmpegInputFormat += info->resourceFormat == blackmagicRawResourceFormatRGBAU8 ? "rgba" : "";
	ffmpegInputFormat += info->resourceFormat == blackmagicRawResourceFormatBGRAU8 ? "bgra" : "";
	ffmpegInputFormat += info->resourceFormat == blackmagicRawResourceFormatRGBU16 ? "rgb48le" : "";
	ffmpegInputFormat += info->resourceFormat == blackmagicRawResourceFormatRGBU16Planar ? "gbrp16le" : "";
	if (info->resourceFormat == blackmagicRawResourceFormatRGBF32 ||
		info->resourceFormat == blackmagicRawResourceFormatRGBF32Planar ||
		info->resourceFormat == blackmagicRawResourceFormatBGRAF32 )
	{
		std::cerr << "FFmpeg format unknown for: " << rawColorFormat << std::endl;
		std::exit(1);
	}

	ffmpegInputFormat += " -s ";
	ffmpegInputFormat += std::to_string(info->width);
	ffmpegInputFormat += "x";
	ffmpegInputFormat += std::to_string(info->height);
	ffmpegInputFormat += " -r ";
	ffmpegInputFormat += std::to_string(info->framerate);
	ffmpegInputFormat += " -i pipe:0 ";
	
	if (info->resourceFormat == blackmagicRawResourceFormatRGBU16Planar)
		ffmpegInputFormat += "-filter:v colorchannelmixer=0:1:0:0:0:0:1:0:1:0:0:0 ";

	std::cout << ffmpegInputFormat;
}
