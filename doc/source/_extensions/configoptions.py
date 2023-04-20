from docutils import nodes
from sphinx.util.docutils import SphinxDirective


def config_ref(pattern):

    if pattern == "global_config":
        global_doc = True
    else:
        global_doc = False

    opt_type = pattern.replace("global_", "")

    def role(name, rawtext, text, lineno, inliner, options={}, content=[]):
        app = inliner.document.settings.env.app

        # FIXME this clearly won't work for non-HTML builders.
        # omit the links in these cases? or figure out something fancy (https://github.com/sphinx-doc/sphinx/issues/10448) ?
        if global_doc:
            document = app.config.options_global_config_doc + ".html"
        else:
            document = ""

        ref_node = nodes.reference(
            rawtext, text, refuri=f"{document}#{opt_type}-{text.lower()}", **options
        )
        return [ref_node], []

    return role


class BaseConfigOption(SphinxDirective):

    has_content = True
    required_arguments = 1
    option_spec = {
        "since": str,
        "choices": lambda x: list(choice.strip() for choice in x.split(",")),
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

        for i in range(len(a_parts)):
            if a_parts[i] < b_parts[i]:
                return False

        return True

    def run(self):
        option_name = self.arguments[0]

        if "choices" not in self.options:
            self.options["choices"] = ["value"]

        target_id = f"{self.opt_type}-{option_name.lower()}"
        target_node = nodes.target("", "", ids=[target_id])

        self.env.app.env.domaindata["std"]["labels"][target_id] = (
            self.env.docname,
            target_id,
            option_name,
        )
        self.env.app.env.domaindata["std"]["anonlabels"][target_id] = (
            self.env.docname,
            target_id,
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

            if not min_since_ver or self.version_at_least(since_ver, min_since_ver):
                text += f"({self.env.app.config.project} >= {since_ver}) "

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


def setup(app):
    app.add_config_value("options_global_config_doc", None, "html")
    app.add_config_value("options_since_ignore_before", None, "html")

    for opt_type in {"config", "dsco", "lco", "co", "oo"}:
        app.add_directive(f"{opt_type}", option_classes[opt_type])
        app.add_role(opt_type, config_ref(opt_type))

    app.add_role("global_config", config_ref("global_config"))
    app.add_role("decl_configoption", decl_configoption("%s"))

    return {"version": "0.1", "parallel_read_safe": True, "parallel_write_safe": True}
