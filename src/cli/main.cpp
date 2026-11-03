// this is just debug for now, honestly

#include <filesystem>
#include <iostream>
#include <string>

#include "Backup.h"

#include <ranges>

int main(int argc, char* argv[])
{
	if (argc != 2)
	{
		std::cerr << "usage: " << argv[0] << " <directory>\n";
		return 1;
	}

	const std::string directory = argv[1];
	if (!std::filesystem::is_directory(directory))
	{
		std::cerr << "error: not a directory: " << directory << "\n";
		return 1;
	}

	const auto bkup = libbackup::Backup(directory);

	const auto& domains = bkup.domains();

	// rdfs
	std::function<void(std::string, const std::unique_ptr<libbackup::BackupFileNode>&)> printoutTree;
	printoutTree = [&](const std::string& indent, const std::unique_ptr<libbackup::BackupFileNode>& node) {
		std::string pathSep = node->isDirectory ? "/" : "";
		std::cout << indent << node->filename << pathSep << "\n";
		for (const auto& childNode : node->children | std::views::values)
		{
			printoutTree(indent + "  ", childNode);
		}
	};

	for (const auto& [name, rootNode] : domains)
	{
		std::cout << "[Domain: '" << name << "']" << "\n";
		printoutTree("", rootNode);
	}

	return 0;
}
