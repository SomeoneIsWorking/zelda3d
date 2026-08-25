#include "error_dialog.h"

#include "extractor/Extract.h"

extern "C" void Messagebox_ShowErrorBox(char* title, char* body) {
    Extractor::ShowErrorBox(title, body);
}
