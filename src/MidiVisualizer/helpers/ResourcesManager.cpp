#include <stdio.h>
#include <iostream>
#include <vector>

#include "ResourcesManager.h"
#include "ProgramUtilities.h"
// Resources headers.
#include "../resources/data.h"

 unsigned char* ResourcesManager::getDataForImage(const std::string & fileName, unsigned int & imwidth, unsigned int & imheight){
	
	if(imagesLibrary.count(fileName) > 0){
		
		auto size = imagesSize[fileName];
		imwidth = int(size[0]);
		imheight = int(size[1]);
		return imagesLibrary[fileName];
	}
	std::cout << "[WARNING]: Unable to find ressource for image \"" << fileName << "\"." << std::endl;
	imwidth = 0;
	imheight = 0;
	return NULL;
}

std::string ResourcesManager::getStringForShader(const std::string & shaderName){
	
	if(shadersLibrary.count(shaderName) > 0){
		return shadersLibrary[shaderName];
	}
	
	std::cout << "[WARNING]: Unable to find ressource for shader \"" << shaderName << "\"." << std::endl;
	return "";
}

void ResourcesManager::loadResources(ID3D11Device* device){

	if (!device)
	{
		std::cout
			<< "[Resources]: Invalid D3D11 device."
			<< std::endl;

		return;
	}

	shadersLibrary.clear();
	imagesLibrary.clear();
	imagesSize.clear();
	textureLibrary.clear();
	
	shadersLibrary = shaders;
	
	imagesLibrary["font"] = font_image;
	imagesLibrary["flash"] = flash_image;
	imagesLibrary["particles"] = particles_image;
	imagesLibrary["noise"] = noise_image;
	imagesLibrary["pedal_side"] = pedal_side_image;
	imagesLibrary["pedal_center"] = pedal_center_image;
	imagesLibrary["pedal_top"] = pedal_top_image;

	imagesLibrary["playButton"] = playButton_image;
	imagesLibrary["pauseButton"] = pauseButton_image;
	imagesLibrary["stopButton"] = stopButton_image;
	imagesLibrary["replayButton"] = replayButton_image;

	imagesSize["font"] = font_size;
	imagesSize["flash"] = flash_size;
	imagesSize["particles"] = particles_size;
	imagesSize["noise"] = noise_size;
	imagesSize["pedal_side"] = pedal_side_size;
	imagesSize["pedal_center"] = pedal_center_size;
	imagesSize["pedal_top"] = pedal_top_size;

	imagesSize["playButton"] = playButton_size;
	imagesSize["pauseButton"] = pauseButton_size;
	imagesSize["stopButton"] = stopButton_size;
	imagesSize["replayButton"] = replayButton_size;
	
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("font", imwidth, imheight);
		textureLibrary["font"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("flash", imwidth, imheight);
		textureLibrary["flash"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("noise", imwidth, imheight);
		textureLibrary["noise"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("particles", imwidth, imheight);
		textureLibrary["particles"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("pedal_side", imwidth, imheight);
		textureLibrary["pedal_side"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("pedal_center", imwidth, imheight);
		textureLibrary["pedal_center"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("pedal_top", imwidth, imheight);
		textureLibrary["pedal_top"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("playButton", imwidth, imheight);
		textureLibrary["playButton"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("pauseButton", imwidth, imheight);
		textureLibrary["pauseButton"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("stopButton", imwidth, imheight);
		textureLibrary["stopButton"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	{
		unsigned int imwidth;
		unsigned int imheight;
		auto image = ResourcesManager::getDataForImage("replayButton", imwidth, imheight);
		textureLibrary["replayButton"] = loadTexture(device, image, imwidth, imheight, 4, false);
	}
	
	{
		unsigned int imwidth1 = 4;
		unsigned int imheight1 = 4;
		unsigned char blankImage[] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
		textureLibrary["blank"] = loadTexture(device, blankImage, imwidth1, imheight1, 4, false);
	}

	{
		unsigned int imwidth1 = 4;
		unsigned int imheight1 = 4;
		unsigned char blankImage[] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 };
		textureLibrary["blankarray"] = loadTextureArray(device, { blankImage }, { glm::ivec2(imwidth1, imheight1) }, 1, false);
	}
}

ID3D11ShaderResourceView* ResourcesManager::getTextureFor(const std::string & fileName){
	if(textureLibrary.count(fileName) > 0){
		return textureLibrary[fileName].Get();
	}
	
	std::cout << "[WARNING]: Unable to find texture for name \"" << fileName << "\"." << std::endl;
	return 0;
}

glm::vec2 ResourcesManager::getTextureSizeFor(const std::string & fileName){
	if(imagesSize.count(fileName) > 0){
		return imagesSize[fileName];
	}
	std::cout << "[WARNING]: Unable to find texture size for name \"" << fileName << "\"." << std::endl;
	return glm::vec2(0.0f,0.0f);
}

std::unordered_map<std::string, std::string> ResourcesManager::shadersLibrary = {};

std::unordered_map<std::string,  unsigned char*> ResourcesManager::imagesLibrary = {};

std::unordered_map<std::string, glm::vec2> ResourcesManager::imagesSize = {};

std::unordered_map<
	std::string,
	ComPtr<ID3D11ShaderResourceView>
> ResourcesManager::textureLibrary = {};
