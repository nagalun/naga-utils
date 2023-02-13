#pragma once

#include <string>
#include <string_view>
#include <map>
#include <functional>

class PropertyReader {
	std::string filePath;
	bool propsChanged;
	std::map<std::string, std::string, std::less<>> props;

	PropertyReader(const PropertyReader&) = default;
	PropertyReader& operator=(const PropertyReader&) = default;

public:
	PropertyReader(std::string_view filePath);

	PropertyReader(PropertyReader&&);

	~PropertyReader();

	bool readFromDisk();
	bool writeToDisk(bool force = false);

	bool isEmpty() const;
	bool hasProp(std::string_view key) const;
	std::string_view getProp(std::string_view key, std::string_view defval = "") const;
	std::string_view getOrSetProp(std::string_view key, std::string_view defval);
	void setProp(std::string_view key, std::string value);
	bool delProp(std::string_view key);

	PropertyReader copy();
	void markAsWritten();
	bool isFileOutdated() const;
};
