import re

from docutils import nodes
from docutils.parsers.rst import Directive
from sphinx.util import logging
from sphinx.util.docutils import SphinxDirective


def option_key(option_type, option_name):
    return f"{option_type}-{option_name}"


class config_reference(nodes.General, nodes.Element):
    """
    Placeholder class marking a reference to a config option, to be resolved later.
    """

    def __init__(self, option_type, option_name, doc):
        super(nodes.General, self).__init__()
        self.option_type = option_type
        self.option_name = option_name
        self.doc = doc


class config_index(nodes.General, nodes.Element):
    """
    Placeholder class marking the location of a config option index, to be created later.
    """

    pass


def config_ref(opt_type):
    def role(name, rawtext, text, lineno, inliner, options={}, content=[]):
        env = inliner.document.settings.env

        # TODO handle ambiguous options, like if two drivers
        # have open options with the same name, and we want
        # to be able to specify which one we mean.
        option = text

        # Record the document from which this reference was
        # used. This lets us build a reverse index showing
        # where each configuration option is used.
        if not hasattr(env, "gdal_option_refs"):
            env.gdal_option_refs = {}

        ref_key = option_key(opt_type, option)

        if ref_key not in env.gdal_option_refs:
            env.gdal_option_refs[ref_key] = []

        env.gdal_option_refs[ref_key].append({"document": env.docname})

        # Emit a placeholder node that describes the config
        # option we're trying to reference. After all files
        # have been parsed and we've discovered where each
        # option is defined, we can go back and replace the
        # placeholder nodes with actual references.
        ref_node = config_reference(opt_type, option, env.docname)

        return [ref_node], []

    return role


def parse_choices(text):
    # split on commas, except those that are escaped with \
    # strip whitespace and remove escape character \
    return [x.replace("\\,", ",").strip() for x in re.split(r"(?<![\\]),", text)]


class BaseConfigOption(SphinxDirective):

    has_content = True
    required_arguments = 1
    option_spec = {
        "since": str,
        "choices": parse_choices,  # FIXME need to ignore commas that are part of the choice, see GTiff DISCARD_LSB, GDAL_GEOREF_SOURCES
        "default": str,
    }

    @staticmethod
    def version_at_least(a, b):
        a_parts = [int(x) for x in a.split(".")]
        b_parts = [int(x) for x in b.split(".")]

        while len(a_parts) < len(b_parts):
            a_parts.append(0)

        while len(b_parts) < len(a_parts):
            b_parts.append(0)

        return a_parts >= b_parts

    def run(self):
        option_name = self.arguments[0]

        if "choices" not in self.options:
            self.options["choices"] = ["value"]

        target_id = f"{self.opt_type}-{option_name.lower()}"
        target_node = nodes.target("", "", ids=[target_id])

        if not hasattr(self.env, "gdal_options"):
            self.env.gdal_options = []

        # Record information about this option and where it was
        # defined in our environment, so we can process cross
        # references later.
        self.env.gdal_options.append(
            {
                "key": option_key(self.opt_type, option_name),
                "option_name": option_name,
                "option_type": self.opt_type,
                "docname": self.env.docname,
                "lineno": self.lineno,
                "target": target_node,
                "target_id": target_id,
            }
        )

        para = nodes.paragraph()
        li_node = nodes.list_item("", para)

        text = f"**{option_name}"

        if len(self.options["choices"]) > 1:
            text += f'=[{"/".join(self.options["choices"])}]**: '
        else:
            text += f'={self.options["choices"][0]}**: '

        if "since" in self.options:
            since_ver = self.options["since"]
            min_since_ver = self.env.app.config.options_since_ignore_before

            try:
                if not min_since_ver or self.version_at_least(since_ver, min_since_ver):
                    text += f"({self.env.app.config.project} >= {since_ver}) "
            except ValueError:
                # TODO figure out how to emit a line number here?
                logger = logging.getLogger(__name__)
                logger.warning(
                    f":since: should be a sequence of integers and periods (got {since_ver})",
                    location=self.env.docname,
                )

        if "default" in self.options:
            text += f'Defaults to {self.options["default"]}. '

        if len(self.content) == 0:
            self.content.append(text, "")
        else:
            self.content[0] = text + self.content[0]

        self.state.nested_parse(self.content, self.content_offset, para)

        return [target_node, li_node]


class ConfigOption(BaseConfigOption):
    opt_type = "config"


class CreationOption(BaseConfigOption):
    opt_type = "co"


class DatasetCreationOption(BaseConfigOption):
    opt_type = "dsco"


class LayerCreationOption(BaseConfigOption):
    opt_type = "lco"


class OpenOption(BaseConfigOption):
    opt_type = "oo"


option_classes = {
    "config": ConfigOption,
    "co": CreationOption,
    "dsco": DatasetCreationOption,
    "lco": LayerCreationOption,
    "oo": OpenOption,
}


def decl_configoption(pattern):
    def role(name, rawtext, text, lineno, inliner, options={}, content=[]):
        children = [nodes.Text(text, text)]
        node = nodes.literal(rawtext, "", *children, role=name.lower(), classes=[name])
        return [node], []

    return role


def purge_option_defs(app, env, docname):
    if not hasattr(env, "gdal_options"):
        return

    env.gdal_options = [x for x in env.gdal_options if x["docname"] != docname]


def purge_option_refs(app, env, docname):
    if not hasattr(env, "gdal_options_refs"):
        return

    for key in env.gdal_option_refs:
        env.gdal_option_refs[key] = [
            x for x in env.gdal_option_refs[key] if x["document"] != docname
        ]


def merge_option_defs(app, env, docnames, other):
    if not hasattr(env, "gdal_options"):
        env.gdal_options = []
    if hasattr(other, "gdal_options"):
        env.gdal_options.extend(other.gdal_options)


def merge_option_refs(app, env, docnames, other):
    if not hasattr(env, "gdal_option_refs"):
        env.gdal_option_refs = {}
    if hasattr(other, "gdal_option_refs"):
        for k, v in other.gdal_option_refs.items():
            if k in env.gdal_option_refs:
                env.gdal_option_refs[k] += v
            else:
                env.gdal_option_refs[k] = v.copy()


def create_config_index(app, doctree, fromdocname):
    env = app.builder.env

    if not hasattr(env, "gdal_option_refs"):
        env.gdal_option_refs = {}

    if not hasattr(env, "gdal_options"):
        env.gdal_options = []

    for node in doctree.findall(config_index):
        content = []

        for opt in env.gdal_options:
            refs = env.gdal_option_refs.get(opt["key"], [])

            # If true, include a link to the definition of
            # the option along with usages. If false, only
            # usages will be linked.
            link_to_definition = True

            if link_to_definition:
                refs.append({"document": opt["docname"]})

            if refs:
                para = nodes.paragraph()
                li_node = nodes.list_item("", para)

                # Link the option back to its definition.
                opt_name = nodes.literal(opt["option_name"], opt["option_name"])
                def_ref = nodes.reference(
                    "",
                    "",
                    refuri=app.builder.get_relative_uri(fromdocname, opt["docname"]),
                )
                def_ref.append(opt_name)
                para += def_ref

                para += nodes.Text(": ")

                # Create a link for each reference to that option.
                # TODO sort by document title instead of document name?
                ref_docs = sorted({ref["document"] for ref in refs})

                for i, ref_doc in enumerate(ref_docs):
                    ref_title = str(env.titles[ref_doc].children[0])

                    ref_node = nodes.reference(
                        "",
                        ref_title,
                        refuri=app.builder.get_relative_uri(fromdocname, ref_doc),
                        internal=True,
                    )
                    if i > 0:
                        para += nodes.Text(", ")
                    para += ref_node

                content.append(li_node)

        node.replace_self(content)


def link_option_refs(app, doctree, fromdocname):
    env = app.builder.env

    logger = logging.getLogger(__name__)

    if not hasattr(env, "gdal_options"):
        env.gdal_options = []

    for node in doctree.findall(config_reference):
        ref_key = option_key(node.option_type, node.option_name)

        matched = False

        # TODO use dict? Would then need to handle check for duplicate declarations
        # both at declaration site and in merge function.
        for opt in env.gdal_options:
            if opt["key"] == ref_key:
                if matched:
                    logger.warning(
                        f"Duplicate definition of {node.option_name} of type {node.option_type}",
                        location=node,
                    )

                from_doc = node.doc
                to_doc = opt["docname"]

                refuri = app.builder.get_relative_uri(from_doc, to_doc)

                ref_node = nodes.reference(
                    "", "", refuri=refuri + "#" + opt["target_id"], internal=True
                )
                ref_text = nodes.literal(opt["option_name"], opt["option_name"])
                ref_node.append(ref_text)

                node.replace_self(ref_node)
                matched = True

        if not matched:
            logger.warning(
                f"Can't find option {node.option_name} of type {node.option_type}",
                location=node,
            )
            text_node = nodes.Text(node.option_name)
            node.replace_self(text_node)


class ConfigIndex(Directive):
    def run(self):
        return [config_index("")]


def setup(app):
    app.add_node(config_reference)
    app.add_node(config_index)

    app.add_config_value("options_global_config_doc", None, "html")
    app.add_config_value("options_since_ignore_before", None, "html")

    app.connect("doctree-resolved", link_option_refs)
    app.connect("doctree-resolved", create_config_index)
    app.connect("env-purge-doc", purge_option_defs)
    app.connect("env-merge-info", merge_option_defs)
    app.connect("env-purge-doc", purge_option_refs)
    app.connect("env-merge-info", merge_option_refs)

    for opt_type, opt_directive in option_classes.items():
        app.add_directive(f"{opt_type}", opt_directive)
        app.add_role(opt_type, config_ref(opt_type))

    app.add_role("decl_configoption", decl_configoption("%s"))

    return {"version": "0.1", "parallel_read_safe": True, "parallel_write_safe": True}
