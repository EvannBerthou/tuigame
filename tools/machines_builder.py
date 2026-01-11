from pathlib import Path
import os, sys

root = 'assets/machines_impl'

machines = sorted(os.listdir(root))

result = []
result.append(len(machines))
for machine in machines:
    result.append('---')
    metadata = os.path.join(root, machine, 'metadata')
    if not os.path.exists(metadata):
        print(f'Metadata file not found for machine {machine}', file=sys.stderr)
        exit()
    result.append(Path(metadata).read_text().strip())

    result.append('files: ')
    fs_folder = os.path.join(root, machine, 'files')
    for path, subdirs, files in os.walk(fs_folder):
        for dir in subdirs:
            name = os.path.join(path, dir)
            name = name[len(fs_folder):]
            result.append(f'- {name}/:')

        for file in files:
            name = os.path.join(path, file)
            content = Path(name).read_text()
            content = content.replace('\n', '\\n')
            name = name[len(fs_folder):]
            result.append(f'- {name}:{content}')

for line in result:
    print(line)
