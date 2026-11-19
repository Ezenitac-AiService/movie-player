#pragma once

#include <string>
#include <vector>

// What happens when a file ends, besides moving on when that's turned on.
enum class RepeatMode { None, One, All };

// Sorts paths the way Explorer does by name, so "2" comes before "10".
void SortByName(std::vector<std::wstring>& paths);

// The videos among these paths, with each folder replaced by the videos in it and in
// all of its subfolders. Sorted by folder first, the way Explorer's folder tree lists
// them, and by name within a folder. Reads folders, so it can be slow on a network drive.
std::vector<std::wstring> CollectVideos(const std::vector<std::wstring>& paths);

// The videos in the same folder whose names differ from this one's only in numbers
// and in a bracketed 8-digit checksum, like the other episodes of a series. Includes
// the video itself, sorted by name. Reads the folder, so it can be slow on a network drive.
std::vector<std::wstring> FindSeries(const std::wstring& video);
