"""Focused render presentation/license guards; no game, lease, device, capture or owner file."""
import pathlib
import tempfile
import unittest

import run

VALID = """[Graphics]
UITextureReplacement = true
SceneTextureReplacement = false
TextureBaseFolder = textures-consolidated-20261018-01
UIScalingMode = 2
"""


class RenderPresentation(unittest.TestCase):
    def test_only_presentation_is_appended(self):
        profile = run.PROFILE + run.render_presentation(VALID)
        self.assertEqual(profile[:len(run.PROFILE)], run.PROFILE)
        self.assertEqual(profile.count("[Controls]"), 1)
        self.assertEqual(profile.count("[Graphics]"), 1)
        self.assertNotIn("[Triple]", profile)  # existing render-test fixture owns the rig

    def test_refused_settings(self):
        for text in (VALID + "[Controls]\nSchema=2\n", VALID + "EnableTextureCache=true\n",
                     VALID + "UITextureExtract=true\n", VALID + "UIScalingMode=2\n",
                     VALID.replace("false", "true"), VALID.replace("UITextureReplacement = true", "UITextureReplacement = false"),
                     VALID.replace("UIScalingMode = 2", "UIScalingMode = 1"),
                     VALID.replace("TextureBaseFolder", "Unknown"),
                     "[DEFAULT]\nUnexpected=1\n" + VALID, "#" * 4097 + "\n" + VALID):
            with self.subTest(text=text[:120]), self.assertRaises(ValueError):
                run.render_presentation(text)

    def test_paths_cannot_escape_or_inject(self):
        for path in ("../owner", "E:/owner", "/owner", "\\owner", "a/b", "a\\b", ".", "..", "a%ENV%", "a b"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                run.render_presentation(VALID.replace("textures-consolidated-20261018-01", path))

    def test_existing_bank_is_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = pathlib.Path(tmp)
            self.assertIsNotNone(run.existing_license_problem(game))
            save = game / "SaveGame"
            save.mkdir()
            for name in ("common.dat", "License1.dat"):
                (save / name).write_bytes(b"fixture")
            self.assertIsNone(run.existing_license_problem(game))
            (save / "other.dat").write_bytes(b"owner data")
            self.assertIsNotNone(run.existing_license_problem(game))
            (save / "other.dat").unlink()
            (save / "common.dat").write_bytes(b"")
            self.assertIsNotNone(run.existing_license_problem(game))

    def test_existing_menu_does_not_guess_after_script(self):
        script = run.EXISTING_LICENSE_SCRIPT
        self.assertNotIn("navDown", [action for _, action in script])
        self.assertEqual(run.next_menu_action(script, 6, 0, 10, 0, allow_fallback=False), "confirm")
        self.assertIsNone(run.next_menu_action(script, 12, 1, .3, 1, allow_fallback=False))
        self.assertIsNone(run.next_menu_action(script, 1000, len(script), 100, len(script), allow_fallback=False))
        receipt = {"plan": {"menuScript": [list(step) for step in script]}, "presses": [{"scripted": True}]}
        self.assertEqual(run.route_of(receipt, "")["script"], [list(step) for step in script])


if __name__ == "__main__":
    unittest.main()
