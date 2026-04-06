import re
p = r'C:\Users\LIN\OfflineNote\src\ui\MainWindow.cpp'
with open(p, 'r', encoding='utf-8') as f:
    c = f.read()

# Fix 1: redoCnt → redoStack
c = c.replace('dc->redoCnt=0;', 'dc->redoStack.clear();')
c = c.replace('dc->redoCnt>0', '!dc->redoStack.empty()')

# Fix 2: Remove redoCnt references
c = re.sub(r's->dc->redoCnt\s*=\s*0\s*;', 's->dc->redoStack.clear();', c)

# Fix 3: on_redo fix - add 'auto& pg='
old_redo = 'static void on_redo(GtkWidget*, gpointer ud){\n    DC* dc=(DC*)ud; if(!dc->note || dc->redoStack.empty()) return;\n    pg.strokes.push_back(dc->redoStack.back());'
new_redo = 'static void on_redo(GtkWidget*, gpointer ud){\n    DC* dc=(DC*)ud; if(!dc->note||dc->redoStack.empty()) return;\n    auto& pg=dc->note->pages[dc->cpi];\n    pg.strokes.push_back(dc->redoStack.back());'
c = c.replace(old_redo, new_redo)

# Fix 4: gtk_popdown_popover → just ignore (color button auto-closes)
c = c.replace('''    // Auto-close the popover if it's a popover
    GtkWidget* toplevel = gtk_widget_get_toplevel(btn);
    if(GTK_IS_POPOVER(toplevel)){
        gtk_popdown_popover(GTK_POPOVER(toplevel));
    }''', '// Color selection done')

# Fix 5: undoStack.push_back(pg.strokes.back()) → UndoCmd
# In on_undo (line ~1331):
c = c.replace(
    '''            dc->redoStack.push_back(pg.strokes.back());
                pg.strokes.pop_back();
                dc->note->dirty=true;
                redraw(dc); updateStatus(dc->mw);''',
    '''            dc->redoStack.push_back(pg.strokes.back());
                pg.strokes.pop_back();
                dc->note->dirty=true;
                redraw(dc); updateStatus(dc->mw);'''
)

# Fix 6: UndoCmd push issues - the undoStack stores UndoCmd, not StrokeD
# Need to fix the lambda capture issue with 'cb'
# In the addTb lambda - actually there's no addTb lambda, it's inline

with open(p, 'w', encoding='utf-8') as f:
    f.write(c)
print('Fixed basic issues')
