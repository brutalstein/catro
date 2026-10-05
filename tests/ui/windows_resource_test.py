"""Deterministic XAML resource checks; native rendering still needs a real window."""

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / "apps/windows/Catro"
X = "{http://schemas.microsoft.com/winfx/2006/xaml}"


def dictionaries(path):
    return {node.get(X + "Key"): node for node in ET.parse(path).iter()
            if node.tag.endswith("ResourceDictionary") and node.get(X + "Key")}


def colors(dictionary):
    return {node.get(X + "Key"): node.get("Color") for node in dictionary
            if node.get("Color", "").startswith("#")}


def luminance(color):
    values = [int(color[index:index + 2], 16) / 255 for index in (1, 3, 5)]
    linear = [value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
              for value in values]
    return sum(value * weight for value, weight in zip(linear, (0.2126, 0.7152, 0.0722)))


class WindowsResources(unittest.TestCase):
    def test_stream_labels_cannot_expand_controls(self):
        names = {node.get(X + "Name"): node for node in ET.parse(ROOT / "Server/ServerView.xaml").iter()}
        controls = names["LocalShareControls"]
        self.assertTrue(controls.tag.endswith("Grid"))
        for name in ("ShareSourceText", "ShareMetaText", "RemoteShareMetaText", "ChannelTitle",
                     "ProfileName", "ProfileRoleText", "VoiceLocalName", "VoiceStateText"):
            with self.subTest(name=name):
                self.assertEqual(names[name].get("TextTrimming"), "CharacterEllipsis")
        self.assertEqual(names["FullScreenStreamButton"].get("Width"), "150")
        toolbar = names["RemoteStreamToolbar"]
        self.assertTrue(any(node.tag.endswith("ScrollViewer") and
                            node.get("HorizontalScrollMode") == "Enabled" for node in toolbar.iter()))

    def test_text_and_primary_button_contrast(self):
        palette = dictionaries(ROOT / "Themes/Palette.xaml")
        controls = dictionaries(ROOT / "MainWindow.xaml")
        for theme, key in (("Light", "Light"), ("Default", "Dark")):
            shades = colors(palette[theme])
            pairs = [(shades[text], shades["CatroBackgroundBrush"])
                     for text in ("CatroTextBrush", "CatroTextSecondaryBrush",
                                  "CatroTextTertiaryBrush", "CatroAccentBrush",
                                  "CatroSageBrush", "CatroRoseBrush")]
            button = colors(controls[key])
            for suffix in ("", "PointerOver", "Pressed"):
                pairs.append((button["AccentButtonForeground" + suffix],
                              button["AccentButtonBackground" + suffix]))
            for foreground, background in pairs:
                bright, dark = sorted((luminance(foreground), luminance(background)), reverse=True)
                with self.subTest(theme=theme, foreground=foreground, background=background):
                    self.assertGreaterEqual((bright + 0.05) / (dark + 0.05), 4.5)

    def test_merged_dictionaries_exist(self):
        for path in ROOT.rglob("*.xaml"):
            for node in ET.parse(path).iter():
                source = node.get("Source")
                if node.tag.endswith("ResourceDictionary") and source:
                    self.assertTrue((path.parent / source).is_file(), f"{path}: {source}")

    def test_no_blue_in_product_palette(self):
        for theme, dictionary in dictionaries(ROOT / "Themes/Palette.xaml").items():
            for key, color in colors(dictionary).items():
                red, green, blue = (int(color[i:i + 2], 16) for i in (1, 3, 5))
                with self.subTest(theme=theme, key=key):
                    self.assertFalse(blue > red + 15 and blue > green)


if __name__ == "__main__":
    unittest.main()
