# Copyright Buckley Builds LLC 2026 All Rights Reserved.
#
# Registers Vibe3D's markdown skill packs as native Unreal Engine 5.8 agent skills, so they are
# discovered through Epic's own ToolsetRegistry.AgentSkillToolset (ListSkills / GetSkills) on the
# MCP endpoint. Each Content/Skills/<name>/SKILL.md becomes one skill; each sibling *.md becomes its
# own sub-doc skill. Re-run with the console command `Vibe3D.ReloadSkills` after editing markdown.

import inspect
import unreal

from pathlib import Path

from toolset_registry.agent_skill import agent_skill

_VIBE3D_SKILL_CLASSES = getattr(unreal, "_vibe3d_skill_classes", None)
if _VIBE3D_SKILL_CLASSES is None:
    _VIBE3D_SKILL_CLASSES = {}
    unreal._vibe3d_skill_classes = _VIBE3D_SKILL_CLASSES

_SKILLS_DIR = Path(__file__).resolve().parent.parent / "Skills"


def _parse_frontmatter(text):
    """Return (description, body) from a markdown file with optional YAML frontmatter."""
    if not text.startswith("---"):
        return "", text.strip()
    close = text.find("\n---", 3)
    if close == -1:
        return "", text.strip()
    frontmatter = text[3:close]
    body = text[close + 4:].lstrip("\r\n")
    description = ""
    for line in frontmatter.splitlines():
        stripped = line.strip()
        if stripped.startswith("description:"):
            description = stripped[len("description:"):].strip().strip('"').strip("'")
            break
    return description, body


def _register_skill(class_suffix, description, instructions):
    """Create (or refresh on reload) a registered UAgentSkill subclass with the given content."""
    existing = _VIBE3D_SKILL_CLASSES.get(class_suffix)
    if existing is not None:
        cdo = existing.get_default_object()
        cdo.set_editor_property("description", inspect.cleandoc(description or class_suffix))
        cdo.set_editor_property("instructions", instructions)
        return
    skill_cls = type("Vibe3D_" + class_suffix, (unreal.AgentSkill,), {
        "__doc__": description or class_suffix,
        "instructions": instructions,
    })
    skill_cls = agent_skill(skill_cls)
    cdo = skill_cls.get_default_object()
    cdo.set_editor_property("description", inspect.cleandoc(description or class_suffix))
    cdo.set_editor_property("instructions", instructions)
    _VIBE3D_SKILL_CLASSES[class_suffix] = skill_cls


def _identifier(value):
    return "".join(ch if ch.isalnum() else "_" for ch in value)


def _register_vibe3d_skills():
    if not _SKILLS_DIR.is_dir():
        unreal.log_warning("Vibe3D: Skills directory not found at {}".format(_SKILLS_DIR))
        return
    count = 0
    for skill_dir in sorted(p for p in _SKILLS_DIR.iterdir() if p.is_dir()):
        skill_md = skill_dir / "SKILL.md"
        if not skill_md.is_file():
            continue
        try:
            description, body = _parse_frontmatter(skill_md.read_text(encoding="utf-8"))
            _register_skill(_identifier(skill_dir.name), description, body)
            count += 1
            for sub in sorted(skill_dir.glob("*.md")):
                if sub.name.lower() == "skill.md":
                    continue
                sub_desc, sub_body = _parse_frontmatter(sub.read_text(encoding="utf-8"))
                _register_skill("{}__{}".format(_identifier(skill_dir.name), _identifier(sub.stem)),
                                "[{} sub-doc] {}".format(skill_dir.name, sub_desc or sub.stem), sub_body)
                count += 1
        except Exception as exc:  # noqa: BLE001 - one bad pack must not break the rest
            unreal.log_warning("Vibe3D: failed to register skill '{}': {}".format(skill_dir.name, exc))
    unreal.log("Vibe3D: registered {} skill(s) with Unreal's AgentSkill system".format(count))


_register_vibe3d_skills()
