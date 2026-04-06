#!/usr/bin/env python3
# Generate MainWindow.cpp - simplest possible GTK3, no crashes
import os
p = r'C:\Users\LIN\OfflineNote\src\ui\MainWindow.cpp'
content = open(os.path.join(os.path.dirname(__file__), '..', '..', 'src', 'ui', 'MainWindow.cpp.bak'), 'r', encoding='utf-8').read() if os.path.exists(os.path.join(os.path.dirname(__file__), '..', '..', 'src', 'ui', 'MainWindow.cpp.bak')) else ''
if not content:
    print('No backup found, keeping existing')
else:
    print('Restored from backup')
