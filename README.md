# Plume

[![](https://dcbadge.vercel.app/api/server/D3KeTqeq6A)](https://discord.gg/D3KeTqeq6A)

Write beautifully. Effortlessly.  
Website: https://www.get-plume.com

## Screenshot

https://github.com/nuttyartist/plume-public/assets/16375940/efb72732-a54c-411e-9436-c007d6283d96

## Is Plume Open Source?

No, Plume is not open source. Plume is based on my previous note-taking app [Notes](https://github.com/nuttyartist/notes), which is fully open source.
In accordance with the Mozilla Public License (MPL) used in Notes, all the source code files from Notes that were modified and incorporated into Plume are made available in this repository. 
This is primarily why this repository exists. Another reason is to track bug reports and feature requests.

## Why use Plume?

I wanted to create a note-taking app with these three points in mind:

1. Its underlying data is a simple plain text (with Markdown syntax).
2. It has a WYSIWYG block editor (that can render complex objects like Kanban, Images, etc).
3. It is simple and familiar to use even for the most non-technical users.

(More details in a future blog post on [my blog](https://rubymamistvalove.com))

## Features

- Native app (written in Qt C++ and QML).
- Beautiful and effortless image support.
- Kanban support.
- Beautiful Drop Cap.
- Drag and drop anything, anywhere (reorder lists, paragraphs, change image location, etc).
- Fast. The fastest block editor out there [according to our test](https://www.get-plume.com).
- All notes are simple plain text strings stored in a local SQLite database formatted in Markdown (with minor syntax for advanced blocks like Kanban and images).
- Markdown support.
- Completely private - tracks nothing. 
- Cross-platform (Linux, macOS, Windows).
- Beautiful interface with attention to detail.
- Folders and Tags. Organize your ideas hierarchically using nested folders and universally using tags.
- Different themes. Switch between Light, Dark, and Sepia.
- Always runs in the background. Use the hotkey <kbd>Win</kbd>+<kbd>Shift</kbd>+<kbd>N</kbd> to summon Plume. <kbd>Ctrl</kbd>+<kbd>N</kbd> for macOS.
- And much more to come.

## Database path

The notes database and settings file are stored in:

| OS      | Path                                                                                                                                                  |
| ------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| Windows | `%APPDATA%\Awesomeness`                                                                                                                               |
| Linux   | `~/.config/Awesomeness`<br>`~/snap/notes/current/.config/Awesomeness` (Snap)<br>`~/.var/app/io.github.nuttyartist.notes/config/Awesomeness` (Flatpak) |
| macOS   | `~/.config/Awesomeness`                                                                                                                               |

## Plume makes use of the following third-party libraries:

[QSimpleUpdater](https://github.com/alex-spataru/QSimpleUpdater)  
[QAutostart](https://github.com/b00f/qautostart)  
[QXT](https://bitbucket.org/libqxt/libqxt/src/master/)  
[MD4C](https://github.com/mity/md4c)  
[html2md (by Tim Gromeyer)](https://github.com/tim-gromeyer/html2md)  
[QBasicHtmlExporter (by Doug Beney)](https://github.com/Open-App-Library/QBasicHtmlExporter)  
[Krita's draganddrop plugin](https://invent.kde.org/graphics/krita/-/tree/master/libs/libqml/plugins/draganddrop)  

## Notes Contributors

Plume is based on the open source [Notes](https://github.com/nuttyartist/notes) app. Thank you to all the contributors who made this possible:

Alex Spataru  
Ali Diouri  
David Planella  
Diep Ngoc  
Guilherme Silva  
Kevin Doyle  
Thorbjørn Lindeijer  
Tuur Vanhoutte  
Waqar Ahmed  
