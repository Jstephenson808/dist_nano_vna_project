"""
Unit tests for VNA Scanner GUI basic functionality
"""
import sys
from pathlib import Path
from unittest.mock import patch, MagicMock

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

    def test_window_title(self, app):
        """Test that the window title is set correctly"""
        assert app.root.title() == "VNA Scanner - Multi-threaded Control"

    def test_left_panel_exists(self, app):
        """Test that left control panel is created"""
        assert app.left_panel is not None
        assert isinstance(app.left_panel, ctk.CTkFrame)

    def test_right_panel_exists(self, app):
        """Test that right plot panel is created"""
        assert app.right_panel is not None
        assert isinstance(app.right_panel, ctk.CTkFrame)

    def test_frequency_entries_exist(self, app):
        """Test that frequency entry fields are created with defaults"""
        assert app.start_freq is not None
        assert app.stop_freq is not None
        assert app.start_freq.get() == "50000000"
        assert app.stop_freq.get() == "900000000"

    def test_scan_control_widgets_exist(self, app):
        """Test that scan control widgets are created"""
        assert app.time_limit is not None
        assert app.points_slider is not None
        assert app.sweep_mode is not None
        assert app.num_sweeps is not None
        assert app.start_button is not None
        assert app.stop_button is not None

    def test_vna_config_widgets_exist(self, app):
        """Test that VNA configuration widgets are created"""
        assert app.num_vnas is not None
        assert app.ports_text is not None
        assert app.num_vnas.get() == "2"

    def test_plot_widgets_exist(self, app):
        """Test that plot widgets are created"""
        assert app.figure is not None
        assert app.ax is not None
        assert app.canvas is not None
        assert app.plot_type is not None
        assert app.stats_label is not None

    def test_log_text_exists(self, app):
        """Test that log textbox exists"""
        assert app.log_text is not None

    def test_theme_menu_exists(self, app):
        """Test that theme menu exists with correct default"""
        assert app.theme_menu is not None
        assert app.theme_menu.get() == "Dark"


class TestGUIHelperMethods:
    """Test GUI helper methods"""

    @pytest.fixture(scope="class")
    def app(self):
        """Create a GUI instance shared across all tests in this class"""
        app = VNAScannerGUI()
        yield app
        if app.root:
            app.root.destroy()

    def test_slider_to_multiplier_at_zero(self, app):
        """Test slider to multiplier conversion at position 0"""
        assert app.slider_to_multiplier(0) == 1

    def test_slider_to_multiplier_at_max(self, app):
        """Test slider to multiplier conversion at position 100"""
        result = app.slider_to_multiplier(100)
        assert result > 1

    def test_slider_to_multiplier_increases_monotonically(self, app):
        """Test that multiplier increases as slider value increases"""
        prev = app.slider_to_multiplier(0)
        for val in [25, 50, 75, 100]:
            current = app.slider_to_multiplier(val)
            assert current >= prev
            prev = current

    def test_slider_to_multiplier_with_invalid_range(self, app):
        """Test slider to multiplier with invalid frequency range"""
        # Save original values
        orig_start = app.start_freq.get()
        orig_stop = app.stop_freq.get()

        # Set invalid range (start > stop)
        app.start_freq.delete(0, "end")
        app.start_freq.insert(0, "1000")
        app.stop_freq.delete(0, "end")
        app.stop_freq.insert(0, "500")

        # Should return 1 for invalid range
        assert app.slider_to_multiplier(50) == 1

        # Restore original values
        app.start_freq.delete(0, "end")
        app.start_freq.insert(0, orig_start)
        app.stop_freq.delete(0, "end")
        app.stop_freq.insert(0, orig_stop)

    def test_slider_to_multiplier_with_non_numeric(self, app):
        """Test slider to multiplier with non-numeric frequency values"""
        orig_start = app.start_freq.get()

        app.start_freq.delete(0, "end")
        app.start_freq.insert(0, "invalid")

        # Should return 1 for invalid input
        assert app.slider_to_multiplier(50) == 1

        app.start_freq.delete(0, "end")
        app.start_freq.insert(0, orig_start)


class TestLogMethod:
    """Test the log method"""

    @pytest.fixture(scope="class")
    def app(self):
        """Create a GUI instance shared across all tests in this class"""
        app = VNAScannerGUI()
        yield app
        if app.root:
            app.root.destroy()

    def test_log_adds_message(self, app):
        """Test that log method adds message to log textbox"""
        # Clear existing log content
        app.log_text.delete("1.0", "end")

        app.log("Test message")
        log_content = app.log_text.get("1.0", "end").strip()
        assert "Test message" in log_content

    def test_log_adds_newline(self, app):
        """Test that log method adds newline after message"""
        app.log_text.delete("1.0", "end")
        app.log("First")
        app.log("Second")
        log_content = app.log_text.get("1.0", "end")
        assert "First\n" in log_content
        assert "Second\n" in log_content


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
        """Test clear_data resets plot and stats"""
        # Modify stats to verify reset
        app.stats_label.configure(text="Points: 100")

        app.clear_data()

        # Check stats are reset
        assert "Points: 0" in app.stats_label.cget("text")

        # Check log was updated
        log_content = app.log_text.get("1.0", "end")
        assert "Data cleared" in log_content

    def test_detect_vnas_with_no_devices(self, app):
        """Test detect_vnas when no devices are found"""
        app.log_text.delete("1.0", "end")

        with patch('glob.glob', return_value=[]):
            app.detect_vnas()

        log_content = app.log_text.get("1.0", "end")
        assert "No VNA devices found" in log_content

    def test_detect_vnas_with_devices(self, app):
        """Test detect_vnas when devices are found"""
        app.log_text.delete("1.0", "end")
        mock_ports = ["/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyACM2"]

        with patch('glob.glob', return_value=mock_ports):
            app.detect_vnas()

        # Check num_vnas is updated
        assert app.num_vnas.get() == "3"

        # Check ports_text is updated
        ports_content = app.ports_text.get("1.0", "end").strip()
        assert "/dev/ttyACM0" in ports_content
        assert "/dev/ttyACM1" in ports_content
        assert "/dev/ttyACM2" in ports_content

        # Check log was updated
        log_content = app.log_text.get("1.0", "end")
        assert "Ready: 3 valid NanoVNA-H device(s)" in log_content

    def test_toggle_sweep_mode_enables_sweeps_field(self, app):
        """Test toggle_sweep_mode enables sweeps field when checked"""
        # Ensure sweep mode is unchecked first
        if app.sweep_mode.get() == 1:
            app.sweep_mode.deselect()
            app.toggle_sweep_mode()

        # Now check sweep mode
        app.sweep_mode.select()
        app.toggle_sweep_mode()

        # num_sweeps should be enabled, time_limit should be disabled
        assert app.num_sweeps.cget("state") == "normal"
        assert app.time_limit.cget("state") == "disabled"

    def test_toggle_sweep_mode_disables_sweeps_field(self, app):
        """Test toggle_sweep_mode disables sweeps field when unchecked"""
        # Ensure sweep mode is checked first
        app.sweep_mode.select()
        app.toggle_sweep_mode()

        # Now uncheck sweep mode
        app.sweep_mode.deselect()
        app.toggle_sweep_mode()

        # num_sweeps should be disabled, time_limit should be enabled
        assert app.num_sweeps.cget("state") == "disabled"
        assert app.time_limit.cget("state") == "normal"

    def test_change_theme_command(self, app):
        """Test change_theme updates appearance mode"""
        with patch.object(ctk, 'set_appearance_mode') as mock_set_mode:
            app.change_theme("Light")
            mock_set_mode.assert_called_with("light")

            app.change_theme("Dark")
            mock_set_mode.assert_called_with("dark")

            app.change_theme("System")
            mock_set_mode.assert_called_with("system")


class TestUpdateScanButtonText:
    """Test update_scan_button_text method"""

    @pytest.fixture(scope="class")
    def app(self):
        """Create a GUI instance shared across all tests in this class"""
        app = VNAScannerGUI()
        yield app
        if app.root:
            app.root.destroy()

    def test_timed_scan_text(self, app):
        """Test button text for timed scan mode"""
        # Ensure sweep mode is off
        if app.sweep_mode.get() == 1:
            app.sweep_mode.deselect()
            app.toggle_sweep_mode()

        # Set time limit to 30
        app.time_limit.delete(0, "end")
        app.time_limit.insert(0, "30")

        app.update_scan_button_text()

        assert app.start_button.cget("text") == "Scan for 30 seconds"

    def test_sweep_scan_text(self, app):
        """Test button text for sweep scan mode"""
        # Enable sweep mode
        app.sweep_mode.select()
        app.toggle_sweep_mode()

        app.update_scan_button_text()

        assert app.start_button.cget("text") == "Start Sweep Scan"

        # Reset sweep mode
        app.sweep_mode.deselect()
        app.toggle_sweep_mode()

    def test_invalid_time_limit_defaults_to_continuous(self, app):
        """Test that invalid time limit defaults to 300 seconds scan text"""
        # Ensure sweep mode is off
        if app.sweep_mode.get() == 1:
            app.sweep_mode.deselect()
            app.toggle_sweep_mode()

        # Set invalid time limit
        app.time_limit.delete(0, "end")
        app.time_limit.insert(0, "invalid")

        app.update_scan_button_text()

        assert app.start_button.cget("text") == "Scan for 300 seconds"

        # Restore valid value
        app.time_limit.delete(0, "end")
        app.time_limit.insert(0, "300")


class TestUpdatePointsDisplay:
    """Test update_points_display method"""

    @pytest.fixture(scope="class")
    def app(self):
        """Create a GUI instance shared across all tests in this class"""
        app = VNAScannerGUI()
        yield app
        if app.root:
            app.root.destroy()

    def test_displays_points_and_scan_info(self, app):
        """Test that display shows total points and scan parameters"""
        app.points_slider.set(100)  # 101 points
        app.update_points_display(None)

        display_text = app.points_display.cget("text")
        assert "101 pts" in display_text
        assert "1 scan" in display_text

    def test_displays_frequency_spacing(self, app):
        """Test that display shows frequency spacing"""
        app.points_slider.set(100)
        app.update_points_display(None)

        display_text = app.points_display.cget("text")
        # Should contain spacing information
        assert "spacing" in display_text

    def test_invalid_frequencies_show_error(self, app):
        """Test that invalid frequencies show appropriate message"""
        orig_start = app.start_freq.get()

        app.start_freq.delete(0, "end")
        app.start_freq.insert(0, "invalid")

        app.update_points_display(None)

        display_text = app.points_display.cget("text")
        assert "Enter valid frequencies" in display_text

        # Restore
        app.start_freq.delete(0, "end")
        app.start_freq.insert(0, orig_start)


class TestTooltip:
    """Test tooltip show/hide functionality"""

    @pytest.fixture(scope="class")
    def app(self):
        """Create a GUI instance shared across all tests in this class"""
        app = VNAScannerGUI()
        yield app
        if app.root:
            app.root.destroy()

    def test_tooltip_initially_none(self, app):
        """Test that tooltip_window is initially None"""
        app.hide_tooltip()  # Ensure clean state
        assert app.tooltip_window is None

    def test_hide_tooltip_destroys_window(self, app):
        """Test that hide_tooltip destroys tooltip window"""
        # Create a mock event
        mock_event = MagicMock()
        mock_event.widget.winfo_rootx.return_value = 100
        mock_event.widget.winfo_rooty.return_value = 100

        # Show tooltip first
        app.show_tooltip(mock_event, "Test tooltip")
        assert app.tooltip_window is not None

        # Hide tooltip
        app.hide_tooltip()
        assert app.tooltip_window is None

    def test_show_tooltip_replaces_existing(self, app):
        """Test that showing a new tooltip replaces the existing one"""
        mock_event = MagicMock()
        mock_event.widget.winfo_rootx.return_value = 100
        mock_event.widget.winfo_rooty.return_value = 100

        # Show first tooltip
        app.show_tooltip(mock_event, "First tooltip")
        first_window = app.tooltip_window

        # Show second tooltip
        app.show_tooltip(mock_event, "Second tooltip")

        # Window should be replaced (not the same object)
        assert app.tooltip_window is not None

        # Cleanup
        app.hide_tooltip()


class TestPlotTypeSelector:
    """Test plot type selector widget"""

    @pytest.fixture(scope="class")
    def app(self):
        """Create a GUI instance shared across all tests in this class"""
        app = VNAScannerGUI()
        yield app
        if app.root:
            app.root.destroy()

    def test_plot_type_default(self, app):
        """Test default plot type selection"""
        assert app.plot_type.get() == "LogMag (S11)"

    def test_plot_type_options(self, app):
        """Test that plot type has expected options"""
        # Set to LogMag S21 and verify it works
        app.plot_type.set("LogMag (S21)")
        assert app.plot_type.get() == "LogMag (S21)"
        # Try Linear and SWR
        app.plot_type.set("Linear (S11)")
        assert app.plot_type.get() == "Linear (S11)"
        app.plot_type.set("SWR (S11)")
        assert app.plot_type.get() == "SWR (S11)"
        # Reset to default
        app.plot_type.set("LogMag (S11)")


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
