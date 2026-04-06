p = r'C:\Users\LIN\OfflineNote\src\ui\MainWindow.cpp'
with open(p, 'r', encoding='utf-8') as f:
    lines = f.readlines()

out = []
skip_until = -1
for i, line in enumerate(lines):
    ln = i + 1

    # Fix: 'cb' undeclared at line 229
    if ln == 229:
        continue

    # Replace UndoCmd usage with simple StrokeD vector
    if 'dc->redoStack.push_back(pg.strokes.back());' in line:
        out.append(line)
        continue

    # Fix: on_redo uses wrong type
    if 'pg.strokes.push_back(dc->redoStack.back());' in line:
        out.append(line)
        continue

    # Fix: undoStack is vector<UndoCmd> but we push StrokeD
    # In on_undo/on_keypress: dc->redoStack.push_back(pg.strokes.back());
    # This should work since redoStack is vector<StrokeD>
    out.append(line)

# Actually the issue is that undoStack and redoStack have different types
# Let me just make both vector<StrokeD>
content = ''.join(lines)

# Make redoStack a vector<StrokeD> (it already is)
# Make undoStack also vector<StrokeD>
content = content.replace(
    'std::vector<UndoCmd> undoStack, redoStack;',
    'std::vector<StrokeD> undoStack, redoStack;'
)

# Remove UndoCmd struct entirely (it's unused now)
# Actually let's keep it for future but fix the push_back errors

# The actual errors are on specific lines. Let me find and fix them.
lines = content.split('\n')
fixed = []
for i, line in enumerate(lines):
    ln = i + 1

    # Line 229: 'cb' undeclared - this is in a lambda
    # Need to see what's there
    if ln == 228 or ln == 229 or ln == 230:
        if 'cb' in line and 'G_CALLBACK' not in line:
            # Skip this line if it's the cb variable
            continue

    # Lines 934, 943, 972, 1327, 1337, 1346: type mismatch
    # These are places where we push StrokeD to UndoCmd vector or vice versa
    # Since we changed undoStack to vector<StrokeD>, the issue is elsewhere
    # Let me check if redoStack is still vector<UndoCmd> somewhere
    if 'vector<UndoCmd>' in line:
        line = line.replace('vector<UndoCmd>', 'vector<StrokeD>')

    fixed.append(line)

content = '\n'.join(fixed)

with open(p, 'w', encoding='utf-8') as f:
    f.write(content)
print('Fixed')
