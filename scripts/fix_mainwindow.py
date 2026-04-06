import re
p = r'C:\Users\LIN\OfflineNote\src\ui\MainWindow.cpp'
with open(p, 'r', encoding='utf-8') as f:
    c = f.read()

# Remove addMenu function
c = re.sub(r'\nstatic void addMenu\(.*?^}\n', '\n', c, flags=re.MULTILINE|re.DOTALL)

# Remove struct array declarations  
c = re.sub(r'\n    const struct \{[^}]*\}\s*\w+Items\[\][^;]*;', '', c, flags=re.DOTALL)

# Replace addMenu calls with inline code
old_menubar = '    GtkWidget* menuBar=gtk_menu_bar_new();\n    addMenu('
if 'addMenu(' in c:
    # Find the addMenu section
    start = c.find('GtkWidget* menuBar=gtk_menu_bar_new();')
    end = c.find('gtk_box_pack_start(GTK_BOX(mainBox),menuBar,FALSE,FALSE,0);')
    if start >= 0 and end >= 0:
        new_code = '''GtkWidget* menuBar=gtk_menu_bar_new();
    {
        GtkWidget* mi=gtk_menu_item_new_with_label("\\xE6\\xAA\\x94\\xE6\\xA1\\x88");
        GtkWidget* m=gtk_menu_new(); GtkWidget* it;
        it=gtk_menu_item_new_with_label("\\xE6\\x96\\xB0\\xE5\\xBB\\xBA\\xE7\\xAD\\x86\\xE8\\xA8\\x98");g_signal_connect(it,"activate",G_CALLBACK(on_newnote),s);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new());
        it=gtk_menu_item_new_with_label("\\xE5\\x8C\\xAF\\xE5\\x87\\xBA PDF");g_signal_connect(it,"activate",G_CALLBACK(on_export_pdf),s->dc);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("\\xE5\\x8C\\xAF\\xE5\\x87\\xBA PNG");g_signal_connect(it,"activate",G_CALLBACK(on_export_png),s->dc);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new());
        it=gtk_menu_item_new_with_label("\\xE7\\xB5\\x90\\xE6\\x9D\\x9F");g_signal_connect(it,"activate",G_CALLBACK(on_quit),s);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi),m);gtk_menu_shell_append(GTK_MENU_SHELL(menuBar),mi);
    }
    {
        GtkWidget* mi=gtk_menu_item_new_with_label("\\xE7\\xB7\\xA8\\xE8\\xBC\\xAF");
        GtkWidget* m=gtk_menu_new(); GtkWidget* it;
        it=gtk_menu_item_new_with_label("\\xE5\\xBE\\xA9\\xE5\\x8E\\x9F Ctrl+Z");g_signal_connect(it,"activate",G_CALLBACK(on_undo),s->dc);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("\\xE9\\x87\\x8D\\xE5\\x81\\x9A Ctrl+Y");g_signal_connect(it,"activate",G_CALLBACK(on_redo),s->dc);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new());
        it=gtk_menu_item_new_with_label("\\xE5\\x88\\xAA\\xE9\\x99\\xA4\\xE9\\x81\\xB8\\xE5\\x8F\\x96");g_signal_connect(it,"activate",G_CALLBACK(on_del),s->dc);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("\\xE6\\x8F\\x92\\xE5\\x85\\xA5\\xE5\\x9C\\x96\\xE7\\x89\\x87");g_signal_connect(it,"activate",G_CALLBACK(on_insert_img),s->dc);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_shell_append(GTK_MENU_SHELL(m),gtk_separator_menu_item_new());
        it=gtk_menu_item_new_with_label("\\xE9\\xA0\\x81\\xE9\\x9D\\xA2\\xE8\\xA8\\xAD\\xE5\\xAE\\x9A");g_signal_connect(it,"activate",G_CALLBACK(on_pageset),s->dc);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi),m);gtk_menu_shell_append(GTK_MENU_SHELL(menuBar),mi);
    }
    {
        GtkWidget* mi=gtk_menu_item_new_with_label("\\xE6\\xAA\\xA2\\xE8\\xA6\\x96");
        GtkWidget* m=gtk_menu_new(); GtkWidget* it;
        it=gtk_menu_item_new_with_label("\\xE6\\x94\\xBE\\xE5\\xA4\\xA7 Ctrl++");g_signal_connect(it,"activate",G_CALLBACK(on_zoomin),s->dc);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("\\xE7\\xB8\\xAE\\xE5\\xB0\\x8F Ctrl+-");g_signal_connect(it,"activate",G_CALLBACK(on_zoomout),s->dc);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        it=gtk_menu_item_new_with_label("\\xE9\\x81\\xA9\\xE5\\x90\\x88\\xE8\\xA6\\x96\\xE7\\xAA\\x97");g_signal_connect(it,"activate",G_CALLBACK(on_zoomfit),s->dc);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi),m);gtk_menu_shell_append(GTK_MENU_SHELL(menuBar),mi);
    }
    {
        GtkWidget* mi=gtk_menu_item_new_with_label("\\xE8\\xAA\\xAA\\xE6\\x98\\x8E");
        GtkWidget* m=gtk_menu_new(); GtkWidget* it;
        it=gtk_menu_item_new_with_label("\\xE9\\x97\\x9C\\xE6\\x96\\xBC OfflineNote");g_signal_connect(it,"activate",G_CALLBACK(on_about),nullptr);gtk_menu_shell_append(GTK_MENU_SHELL(m),it);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi),m);gtk_menu_shell_append(GTK_MENU_SHELL(menuBar),mi);
    }'''
        c = c[:start] + new_code + c[end:]

# Fix: remove 'GtkWidget* btn=...' from addTbItem since we changed it
# Actually addTbItem is fine, just need to ensure no type issues

# Fix: remove "const struct" declarations that remain
c = re.sub(r'    const struct \{.*?\n', '', c)

with open(p, 'w', encoding='utf-8') as f:
    f.write(c)
print('Fixed MainWindow.cpp')
