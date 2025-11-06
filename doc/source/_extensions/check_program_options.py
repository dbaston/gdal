import os

import sphinx.addnodes
from sphinx.util import logging

from osgeo import gdal


def find_options(node):
    found_nodes = set()

    found_nodes.add(type(node))

    for child in node.children:
        found_nodes |= find_options(child)

    return found_nodes


def handler(app, doctree):
    dirname, fname = os.path.split(doctree.attributes["source"])

    if not dirname.endswith("programs"):
        return

    docname = os.path.splitext(fname)[0]

    try:
        algname = docname
        algname = algname.replace("fill_nodata", "fill-nodata")
        algname = algname.replace("as_features", "as-features")
        algname = algname.replace("_", " ")
        alg = gdal.Algorithm(algname)
    except RuntimeError as e:
        if "not a valid" in str(e):
            return
        else:
            raise

    options = doctree.traverse(sphinx.addnodes.desc_signature)

    if not options:
        # TODO check?
        return

    # is every argument name documented?
    for arg in alg.GetArgNames():
        if arg in {
            "input",
            "output",
            "help",
            "help-doc",
            "json-usage",
            "config",
            "quiet",
            "progress",
        }:
            continue

        matches = 0

        for opt in options:
            if f"--{arg}" in opt.attributes["allnames"]:
                matches += 1

        if matches == 1:
            continue

        logger = logging.getLogger(__name__)
        if matches == 0:
            logger.warning(
                f"Option {arg} of {alg.GetName()} is not documented",
                location=app.env.docname,
            )
        else:
            logger.warning(
                f"Option {arg} of {alg.GetName()} is documented multiple times",
                location=app.env.docname,
            )

    # does every documented argument actually exist?
    for opt in options:
        for altopt in opt.attributes["allnames"]:
            if not alg.GetArg(altopt.lstrip("-")):
                logger = logging.getLogger(__name__)
                logger.warning(f"Option {altopt} is not supported by {alg.GetName()}")


def setup(app):
    app.connect("doctree-read", handler)
