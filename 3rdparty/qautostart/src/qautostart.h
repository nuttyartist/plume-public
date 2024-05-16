// The MIT License (MIT)
//
// Copyright (C) 2016 Mostafa Sedaghat Joo (mostafa.sedaghat@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef AUTOSTART_H
#define AUTOSTART_H

#include <QString>

#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
#include <QTextStream>

namespace Qt {
    using ::endl;
}
#endif

class Autostart
{
public:
    bool isAutostart() const;
    void setAutostart(bool autostart);
#if defined (Q_OS_LINUX)
    QString getAutostartDir() const;
    bool isFlatpak() const;
#endif

protected:
    QString appPath() const;
    QString appName() const;
 };

#endif // AUTOSTART_H
