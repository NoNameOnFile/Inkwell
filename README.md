# Inkwell

Inkwell is a writing desk. It underlines spelling, grammar, and style notes while you write.

The window is the usual way to run it. A terminal desk is included for sessions without a display. Spell check uses the system Hunspell dictionary (`/usr/share/hunspell/en_US` by default). Grammar notes are local.

## Build

Qt 6 Widgets is required for the window.

```sh
cmake -S . -B build
cmake --build build
./build/inkwell
```

Open a file with `./build/inkwell draft.txt`. On Linux without `DISPLAY` or `WAYLAND_DISPLAY`, the window tells you to use the terminal desk:

```sh
./build/inkwell-tui draft.txt
```

Review a file without opening either desk:

```sh
./build/inkwell --check draft.txt
```

The same `--check` switch works on `inkwell-tui`. Qt is the Linux dependency used here, and the same Widgets project is what you compile on Windows and macOS. This tree has been built on Linux.

A Flatpak build uses the manifest in the project root. Run it from that directory, not from `build`:

```sh
flatpak-builder --user --install --install-deps-from=flathub build-flatpak org.inkwell.Inkwell.yaml
```

The first run downloads the KDE 6.11 SDK. Spell check inside the Flatpak uses the English dictionary shipped in `packaging/hunspell`.

That install stays on your machine. To post a file someone else can install, bundle the build that is already in `.flatpak-builder/cache`:

```sh
flatpak build-bundle .flatpak-builder/cache dist/Inkwell-0.1.0.x86_64.flatpak org.inkwell.Inkwell master --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo
```

The file is the app only. The person installing it still gets the KDE 6.11 runtime from Flathub:

```sh
flatpak install --user Inkwell-0.1.0.x86_64.flatpak
```

## Window

The page is set like a novel: a serif column, indented paragraphs, and no line numbers. A chapter saves as prose, one paragraph per line, with blank lines left as they are. Italics and bold are tags on those words. A picture or a data field is a tag on its own line. A scene break is a line of `* * *`. Markup on the page shows the tags. Qt’s text document already reads and writes this markup, so the desk stays on Qt.

```html
She <i>waited</i> at the door.

* * *
<field name="pov">Emiri</field>
<img src="images/tower.png" alt="The tower">
```

The desk opens on the Classic theme, in a light room. View → Theme switches the whole window among Classic, Modern, Bubbly, and Industrial. Each one has its own desk, paper, ink, accent, and corner shape. Light and Dark, at the right of the menu bar and under View, stay with the theme you picked. Both choices are remembered the next time you open Inkwell.

View → Page, or Ctrl-comma, sets the writing page. Typeface lists the writing faces on this machine. Size, line spacing, first-line indent, and justified or ragged text are kept in `~/.config/inkwell/page`. Column width is the widest the centered page grows. Paper and ink follow the window theme until you pick your own. Reset returns the page to the desk defaults.

The page sits on the desk, with a drawer on each side. Click a vertical tab to open it, and click the open tab again to tuck it away.

Inkwell opens on the shelf. New book asks for a title and saves the book in your Documents folder, inside Inkwell, which is created the first time you need it. Custom location picks a different folder. Open book and the cards on the shelf return you to a book you already keep. File → Shelf closes the open book and asks whether to save it before coming back to that row.

The Book menu and the Book tab in the left drawer hold the details used to ready a book for export: title, author, edition, year, publisher, ISBNs, and the clauses that belong on a copyright page. Save, the editing commands, and the Book menu stay dim on the shelf, where no book is open. Choose Cover stays dim until the book has been saved, because the cover is stored with the book. Copyright and Contents show those pages in the writing column. Choose Cover stores a cover with the book. Export Book opens a publishing page in the writing column. Choose Print PDF, EPUB, Word, or the manuscript, then the trim size and margins for print, and which parts go in: the cover, title page, copyright, contents, and any of the chapters. Print PDF and Word use that trim. Body size starts at 11 point with book leading, which is the size of a trade paperback, separate from the larger type on the writing desk. The typeface still comes from the writing page. EPUB reflows for the reader. The manuscript stays a text file. The last export choices are kept in `~/.config/inkwell/export`.

The left drawer holds chapters and story notes. Chapters are the pages of the book. Notes are an outline, grouped into character, plot, worldbuilding, and general. Pick one and the page shows that piece. A single chapter still saves as one text file. Once there is another chapter or a note, Save offers a folder in Documents/Inkwell, or a custom location. That folder holds `inkwell.book`, which names the chapters and notes, and a text file for each one. The chapter text stays prose; tags appear only where the formatting is. After the first Save, Inkwell writes again about two seconds after you pause, and at least every twenty seconds while you keep typing. The same pause stores a recovery copy even before that first Save. If the desk does not close cleanly, the next launch asks whether to bring the writing back. File → Open Recent lists the files and books you have opened.

The right drawer holds suggestions. Suggestions lists spelling, grammar, style, and possible names. Apply replaces the marked words with the first suggestion. Add name keeps a word in the dictionary. A capitalized word that is not a close typo is marked as a possible name. A pause of about a third of a second lets the word you are typing finish before it is marked.

| Key | Action |
| --- | --- |
| Ctrl-N / Ctrl-O | New / open a text file |
| Shelf | Return to the shelf of books |
| Open Book | Open a saved book folder |
| Book → Export Book | Choose the format, trim size, and what goes in the book |
| Open Recent | Open a file or book you used before |
| Ctrl-S / Ctrl-Shift-S | Save / save as |
| Ctrl-Z / Ctrl-Y | Undo / redo |
| Ctrl-Shift-L / Ctrl-Shift-I | Learn / ignore the word at the cursor |
| Edit → Add Name… | Keep a name in the dictionary |
| Ctrl-I / Ctrl-B | Italic / bold |
| View → Theme | Classic, Modern, Bubbly, or Industrial |
| Ctrl-, | Page settings: type, column, colors |
| F2 / F3 | Next / previous note |
| F4 | Apply the suggestion |
| Ctrl-Q | Quit |

## Terminal

`inkwell-tui` is the same checker in the terminal.

| Key | Action |
| --- | --- |
| F1 | Help |
| F2 / F3 | Next / previous note |
| F4 | Apply the suggestion |
| Ctrl-P | Command bar |
| Ctrl-Space | Start or clear a selection |
| Ctrl-S / Ctrl-O | Save / open |
| Ctrl-Z / Ctrl-Y | Undo / redo |
| Ctrl-Q | Quit |

Commands from Ctrl-P: `learn`, `ignore`, `save`, `open`, `copy`, `quit`.

## Dictionary

`learn` and Edit → Add Name store a word in `~/.config/inkwell/personal.dic`. A capitalized word that is not one keystroke from a dictionary word is treated as a possible name. `ignore` hides a word until you quit. `INKWELL_DICT=en_GB` selects another dictionary in `/usr/share/hunspell`.

## Plugins

A plugin is a shared library that adds notes to the same list as spelling and grammar. Inkwell checks the plugins folder when it opens. Drop a `.so` file there and start Inkwell again.

The folder is `~/.local/share/inkwell/plugins`. Inside the Flatpak it is `~/.var/app/org.inkwell.Inkwell/data/inkwell/plugins`. `INKWELL_PLUGINS` names one folder and, when set, is the only folder checked. An installed copy also looks beside the program, in `lib/inkwell/plugins`.

`plugins/sample.cpp` is a plugin that marks the word `plugin-mark`. A plugin exports `inkwell_plugin` and returns the struct in `src/plugin.h`. `begin` and `end` are byte offsets on the page. `kind` is 0 for spelling, 1 for grammar, 2 for style, or 3 for a name. A replacement, when the plugin gives one, is what Apply offers. A library that does not export that symbol is skipped, and the status bar names the file.
