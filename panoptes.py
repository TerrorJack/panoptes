#!/usr/bin/env python3

import pathlib
import json
import argparse


def is_valid_compiler(path):
    """
    We need to check compiler name, to rule out ghc invocations that
    compile c/c++
    """
    valid_compilers = {"c++", "cc", "clang", "clang++", "g++", "gcc"}
    return pathlib.Path(path).name in valid_compilers


def is_valid_source(arg):
    """
    Check for extensions
    """
    valid_extensions = {".c", ".c++", ".cc", ".cpp", ".cxx"}
    return pathlib.Path(arg).suffix in valid_extensions


def canonicalize_path(cwd, path):
    """
    Make a path absolute, return None if it's non-existent
    """
    cwd = pathlib.Path(cwd)
    path = pathlib.Path(path)
    p = path if path.is_absolute() else cwd.joinpath(path)

    try:
        return p.resolve(strict=True).as_posix()
    except:
        return None


def parse_entries(obj):
    """
    Parse a deserialized events.jsonl line, return an iterable of
    compile_commands.json entry objects
    """
    arguments = obj["arguments"]
    if pathlib.Path(arguments[0]).name == "ccache":
        arguments.pop(0)
    if not is_valid_compiler(arguments[0]):
        return []
    if not any(arg == "-c" for arg in arguments[1:]):
        return []
    srcs = (
        canonicalize_path(obj["directory"], arg)
        for arg in arguments[1:]
        if is_valid_source(arg)
    )
    return (
        {
            "arguments": arguments,
            "directory": obj["directory"],
            "file": src,
        }
        for src in filter(None, srcs)
    )


def update_compdb(update_func, compdb, obj):
    """
    Update a compdb object using an update function.

    A compdb object is not the array of object that's serialized in
    the compiled_commands.json. It's a dict that maps the "file" field
    to that source file's entry object, to guarantee each source file
    has only one entry in the final output, otherwise clangd gets
    confused.

    An update function takes previous entry and current entry and
    returns the final entry. It's called if compdb already contains
    that file's entry. Particularly useful in build systems like
    hadrian that compile a same c/c++ source file with multiple
    different flavours and you want to work with a single flavour.
    """
    entries = parse_entries(obj)
    for entry in entries:
        compdb[entry["file"]] = (
            update_func(compdb[entry["file"]], entry)
            if entry["file"] in compdb
            else entry
        )


def update_func(prev_entry, curr_entry):
    """
    The update function used in update_compdb. Prefers the entry that
    activates the cpp-guarded code paths of threaded/profiling/debug
    flavours, if they are indeed built by hadrian. Falls back to the
    current entry.
    """
    make_pred = lambda pred: lambda entry: any(
        arg == pred for arg in entry["arguments"]
    )
    is_threaded = make_pred("-DTHREADED_RTS")
    is_profiling = make_pred("-DPROFILING")
    is_debug = make_pred("-DDEBUG")

    for pred in [is_threaded, is_profiling, is_debug]:
        match pred(prev_entry), pred(curr_entry):
            case False, True:
                return curr_entry
            case True, False:
                return prev_entry

    return curr_entry


def make_compdb(compdb_path, events_path):
    """
    Generate compile_commands.json from events.jsonl.
    """
    try:
        with open(compdb_path) as f:
            prev_compdb = json.load(f)
            compdb = {entry["file"]: entry for entry in prev_compdb}
    except:
        compdb = {}
    with open(events_path) as f:
        for l in f:
            update_compdb(update_func, compdb, json.loads(l))
    with open(compdb_path, "w") as f:
        json.dump([entry for entry in compdb.values()], f, indent=2, sort_keys=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate compile_commands.json from events.jsonl"
    )
    parser.add_argument(
        "-i",
        "--input",
        help="Input file path, defaults to events.jsonl",
        default="events.jsonl",
    )
    parser.add_argument(
        "-o",
        "--output",
        help="Output file path, defaults to compile_commands.json",
        default="compile_commands.json",
    )
    args = parser.parse_args()
    make_compdb(args.output, args.input)
