#pragma once

// Felület nyelve. A forráskódban az angol szöveg a kulcs; T() a kiválasztott nyelvű változatot adja vissza
// (ha nincs fordítás, az angolt). A visszaadott mutató a program teljes futása alatt érvényes.
enum Language { kLangEnglish = 0, kLangHungarian = 1, kLangCount };

void setLanguage(int lang);
int language();
const char* T(const char* english);
const char* languageName(int lang);  // a nyelv neve a saját nyelvén (pl. "Magyar")
