#ifndef Configuration_h
#define Configuration_h

#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

typedef std::unordered_map<std::string, std::vector<std::string>> Arguments;

// Helper to trim characters from both ends of a string.
std::string trim(const std::string & str, const std::string & del);

std::string join(const std::vector<std::string>& strs, const std::string& delim);

class Configuration {

public:

	Configuration(const std::vector<std::string>& argv);

	void save();

	const Arguments& args() const { return _args; }

	static Arguments parseArguments(std::istream & configFile);

	static Arguments parseArguments(const std::vector<std::string> & argv, bool allowEmpty);

	static bool parseBool(const std::string & str);

	static int parseInt(const std::string & str);

	static float parseFloat(const std::string & str);

	static glm::vec3 parseVec3(const std::vector<std::string> & strs);

	static void printVersion();

	static void printHelp();

	const std::string& getConfigFilePath() const { return lastConfigPath; }
	
public:

	// General settings (will be saved)
	std::string lastMidiPath;
	std::string lastMidiDevice;
	std::string lastConfigPath;
	glm::ivec2 windowSize = { 1280, 600 };
	glm::ivec2 windowPos = {100, 100};
	float guiScale = 1.0f;
	bool useTransparency = false;
	bool showVersion = false;
	bool showHelp = false;

private:

	Arguments _args;

};

#endif
