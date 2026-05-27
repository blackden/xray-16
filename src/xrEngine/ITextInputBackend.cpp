#include "stdafx.h"
#pragma hdrstop

#include "ITextInputBackend.h"

// Out-of-line empty destructor for the interface, per project
// convention (doc/procedure/cpp_code.txt: "у интерфейсных классов
// деструктор pure virtual + пустая inline-реализация вне класса").
ITextInputBackend::~ITextInputBackend() = default;
