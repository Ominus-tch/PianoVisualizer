#include <stdio.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

int main() {
	const std::string resourcesDir = "resources/";
	const std::string outputDir = "src/MidiVisualizer/resources/";

	std::vector<std::string> shadersToLoad = { "flashes", "notes", "particles", "particlesblur", "screenquad", "majorKeys", "minorKeys", "pedal", "wave", "fxaa", "wave_noise", "score_bars", "score_labels", "background" };

	std::ofstream shadersOutput(outputDir + "shaders.cpp");
	if (!shadersOutput.is_open()) {
		std::cerr << "Unable to open handle to shaders output file." << std::endl;
		return 1;
	}
	shadersOutput << "#include \"data.h\"\n\n"
		<< "const std::unordered_map<std::string, std::string> shaders = {\n\n";

	for (size_t sid = 0; sid < shadersToLoad.size(); ++sid) {
		const auto& shaderName = shadersToLoad[sid];
		const std::string shaderBasePath = resourcesDir + "shaders/" + shaderName;
		std::ifstream vertShader(shaderBasePath + ".vert");
		std::ifstream fragShader(shaderBasePath + ".frag");
		if (!vertShader.is_open() || !fragShader.is_open()) {
			std::cerr << "Unable to open handle to shaders input file for " << shaderBasePath << "." << std::endl;
			continue;
		}
		std::string buffLine;

		// Vertex shader content.
		shadersOutput << "	{ \"" << shaderName << "_" << "vert" << "\", \"";
		while (std::getline(vertShader, buffLine)) {
			if (buffLine.empty()) {
				continue;
			}
			shadersOutput << buffLine << "\\n ";
		}
		shadersOutput << "\"}, " << "\n";

		// Fragment shader content.
		shadersOutput << "	{ \"" << shaderName << "_" << "frag" << "\", \"";
		while (std::getline(fragShader, buffLine)) {
			if (buffLine.empty()) {
				continue;
			}
			shadersOutput << buffLine << "\\n ";
		}
		shadersOutput << "\"}" << (sid == shadersToLoad.size() - 1 ? "" : ",") << "\n\n";

	}

	shadersOutput << "};" << "\n";
	shadersOutput.close();
	return 0;
}


