#pragma once

// Lists the app under "Open with" and in Windows' Default Apps settings, for the
// current user only. This never changes which app is the default: Windows only
// lets the user do that, and existing associations are left as they are.

// True if registered, and pointing at this copy of the program.
bool IsFileAssociationRegistered();
// True if anything that registering writes is there, from this copy or from any other,
// such as an older version or one that has since been moved. Unregistering removes it.
bool HasFileAssociationEntries();
bool RegisterFileAssociation();
void UnregisterFileAssociation();
void OpenDefaultAppsSettings();
