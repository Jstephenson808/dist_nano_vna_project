"""
Unit tests for VNA Scanner GUI basic functionality
"""
import sys
from pathlib import Path

# Add the src directory to the path so we can import the GUI module
sys.path.insert(0, str(Path(__file__).parent.parent.parent / "src" / "VnaScanGUI"))

import pytest
import customtkinter as ctk
from vnaScanGUI import VNAScannerGUI


class TestGUIBasics:
    """Test basic GUI initialization and structure"""
    
    @pytest.fixture(scope="class")
    def app(self):
        """Create a GUI instance shared across all tests in this class"""
        app = VNAScannerGUI()
        yield app
        # Cleanup: destroy the window after all tests in class complete
        if app.root:
            app.root.destroy()
    
    def test_gui_initializes(self, app):
        """Test that the GUI initializes without errors"""
        assert app.root is not None
        assert isinstance(app.root, ctk.CTk)


class TestGUIHelperMethods:
    """Test GUI helper methods"""
    
    @pytest.fixture(scope="class")
    def app(self):
        """Create a GUI instance shared across all tests in this class"""
        app = VNAScannerGUI()
        yield app
        if app.root:
            app.root.destroy()
    
    def test_slider_to_multiplier(self, app):
        """Test slider to multiplier conversion"""
        # At position 0, multiplier should be 1
        assert app.slider_to_multiplier(0) == 1
        # At position 100, multiplier should be greater than 1
        assert app.slider_to_multiplier(100) > 1


class TestGUICommands:
    """Test GUI button commands and interactions"""
    
    @pytest.fixture(scope="class")
    def app(self):
        """Create a GUI instance shared across all tests in this class"""
        app = VNAScannerGUI()
        yield app
        if app.root:
            app.root.destroy()
    
    def test_clear_data_command(self, app):
        pass
    
    def test_detect_vnas_command(self, app):
        pass

    def test_toggle_sweep_mode_command(self, app):
        pass
    
    def test_change_theme_command(self, app):
        pass

if __name__ == "__main__":
    pytest.main([__file__, "-v"])
