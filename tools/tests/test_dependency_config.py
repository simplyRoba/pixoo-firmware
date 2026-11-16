"""Regression checks for the shared Python tooling manifest."""
import importlib.metadata
import json
from pathlib import Path
import re
import tomllib
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[2]


def normalize(name):
    return re.sub(r"[-_.]+", "-", name).lower()


class DependencyConfigTest(unittest.TestCase):
    def test_dev_dependencies_are_pinned_roots_not_esphome_dependencies(self):
        project = tomllib.loads((ROOT / "pyproject.toml").read_text())
        dependencies = project["dependency-groups"]["dev"]
        pins = {}
        for dependency in dependencies:
            match = re.fullmatch(r"([A-Za-z0-9][A-Za-z0-9._-]*)==([^;\s]+)", dependency)
            self.assertIsNotNone(match, dependency)
            name = normalize(match.group(1))
            self.assertNotIn(name, pins)
            pins[name] = match.group(2)
        self.assertEqual(set(pins), {"esphome", "cairosvg"})

        requirements = importlib.metadata.requires("esphome")
        self.assertIsNotNone(requirements)
        owned = {normalize(re.match(r"[A-Za-z0-9][A-Za-z0-9._-]*", item).group())
                 for item in requirements}
        self.assertFalse(set(pins) & owned, set(pins) & owned)

    def test_python_versions_and_ci_install_commands(self):
        project = tomllib.loads((ROOT / "pyproject.toml").read_text())
        minor = re.fullmatch(r"==(\d+\.\d+)\.\*", project["project"]["requires-python"])
        self.assertIsNotNone(minor)
        workflow = yaml.load((ROOT / ".github/workflows/firmware-ci.yml").read_text(),
                             Loader=yaml.BaseLoader)
        jobs = workflow["jobs"]
        expected_jobs = {"core-tests", "esphome-build", "render-tests"}
        self.assertTrue(expected_jobs <= jobs.keys())
        for name, job in jobs.items():
            steps = job.get("steps", [])
            setups = [step for step in steps if step.get("uses", "").startswith("actions/setup-python@")]
            if name in expected_jobs:
                self.assertTrue(setups, name)
            for step in setups:
                self.assertEqual(step["with"]["python-version"], minor.group(1), name)
            commands = [line.strip() for step in steps for line in step.get("run", "").splitlines()
                        if re.search(r"\b(?:python\S*\s+-m\s+)?pip\s+install\b", line)]
            if name in expected_jobs:
                self.assertTrue(any(re.search(r"--group\s+dev\b", line) for line in commands), name)
            for line in commands:
                if line != "python -m pip install --upgrade pip":
                    self.assertRegex(line, r"--group\s+dev\b", name)

    def test_release_version_and_ci_paths(self):
        text = (ROOT / "pyproject.toml").read_text()
        version = tomllib.loads(text)["project"]["version"]
        self.assertEqual(version, (ROOT / "version.txt").read_text().strip())
        self.assertEqual(version, json.loads((ROOT / ".release-please-manifest.json").read_text())["."])
        self.assertEqual(text.count("x-release-please-version"), 1)
        self.assertRegex(text, rf'(?m)^version\s*=\s*["\']{re.escape(version)}["\']\s*#\s*x-release-please-version\s*$')
        config = json.loads((ROOT / "release-please-config.json").read_text())
        self.assertIn({"type": "generic", "path": "pyproject.toml"},
                      config["packages"]["."]["extra-files"])
        workflow = yaml.load((ROOT / ".github/workflows/firmware-ci.yml").read_text(),
                             Loader=yaml.BaseLoader)
        self.assertIn("pyproject.toml", workflow["on"]["push"]["paths"])


if __name__ == "__main__":
    unittest.main()
