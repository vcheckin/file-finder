import os
import random
import string
from pathlib import Path

def random_name(length):
    return ''.join(random.choices(string.ascii_lowercase, k=length))

def make_linear_path(base: Path, depth: int, name_len: int) -> Path:
    current = base
    for d in range(depth):
        name = "dl" + str(d) + "_" + random_name(name_len)
        current = current / name
        current.mkdir()
    return current

# /d1/d2.../d_k/{l subdirs}/d1/d2/.../dk/{m subdirs}/{n_files files}
#
def create_structure(root: str, k: int, l: int, m: int, n_files: int, fn_len: int):
    root_path = Path(root)
    root_path.mkdir(parents=True, exist_ok=True)

    base_leaf = make_linear_path(root_path, k, fn_len)
    # branch out l times
    for id in range(l):
        # create each l subdir at k-th level
        subdir = base_leaf / ("L" + str(id) + "_" + random_name(fn_len))
        subdir.mkdir()

        leaf = make_linear_path(subdir, k, fn_len)
        saved_dir = os.getcwd()
        os.chdir(leaf)

        # branch again m times
        for im in range(m):
            sub = Path(".") / ("M" + str(im) + "_" + random_name(fn_len))
            sub.mkdir()

            # and create n files
            for ff in range(n_files):
                file = sub / ("F" + str(ff) +"_" + random_name(fn_len) + '.txt')
                file.touch()
        os.chdir(saved_dir)

if __name__ == "__main__":
    create_structure(
        root="test_hierarchy",
        k=10,
        l=1024,                    # branches at middle level
        m=10,                    # secondary depth
        n_files=300,             # files in each subdir
        fn_len=32                     # name length
    )
