# Screen Text 2

Select any part of the screen and copy its text. It uses the OCR engine built into Windows 10/11, so it works offline.

## Shortcuts
- **Ctrl+Alt+T**: capture. Drag over the text, click once to read the whole screen, or press Esc to cancel.
- **Ctrl+Alt+Shift+T**: read the same area again (useful for content that changes).
- **Ctrl+V** (in the window): read an image from the clipboard, for example after Win+Shift+S.

## What is new in version 2
- **No size limit.** Large areas and full 4K screens are split into overlapping tiles. Each tile is read separately, then the results are joined, with duplicate words removed and split lines rejoined. Version 1 showed "Area too large".
- **Automatic text size.** It measures the text height and reads again at the size Windows OCR handles best. Tiny text is enlarged up to 4x, and huge text is shrunk.
- **Image clean-up.** Light text on a dark background (dark mode) is inverted, contrast is stretched, and a white margin is added so text touching the edge is not lost.
- **Accurate mode (default).** It also tries a local-background-removal version, which helps with coloured buttons, highlighted rows and mixed light/dark areas, plus the original image. It keeps whichever result has the most text. The extra passes run in parallel.
- **Language menu.** Choose any installed OCR language, or let it try all of them and keep the best. Your choice is remembered.
- **Three layouts.** *Lines* keeps line breaks and blank lines between paragraphs. *Paragraphs* joins wrapped lines and repairs words hyphenated across a line break. *Keep layout* keeps columns and indentation for tables and code.
- Chinese and Japanese text has no stray spaces between characters, and right-to-left text keeps its order.

Settings are saved in `%APPDATA%\QRClipTools\screentext.txt`.

## Building
Install MSYS2 and run `pacman -S mingw-w64-ucrt-x86_64-gcc`. Then run `build.bat` in the "MSYS2 UCRT64" terminal.
`screentext/ocrcore.h` holds the engine-independent pipeline. `tests/harness.cpp` runs that pipeline on Linux with Tesseract as a stand-in engine:
`g++ -O2 -std=c++17 -pthread tests/harness.cpp -o harness && ./harness image.ppm acc 0`
