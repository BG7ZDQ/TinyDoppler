using System;
using System.Drawing;
using System.Globalization;
using System.Windows.Forms;
using Microsoft.Win32;
using SDRSharp.Common;

namespace SDRSharp.AstroSeriesBridge
{
    internal sealed class BridgeControlPanel : UserControl
    {
        private readonly IqBridgeProcessor _processor;
        private readonly ISharpControl _control;
        private readonly DopplerControlReader _doppler;
        private readonly CheckBox _enabled;
        private readonly CheckBox _autoDoppler;
        private readonly Label _status;
        private readonly Timer _timer;

        private static string TextFor(string chinese, string english, string japanese)
        {
            string language = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
            return language == "zh" ? chinese : language == "ja" ? japanese : english;
        }

        private static bool LoadAutoDoppler()
        {
            try
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(@"Software\TinyDoppler"))
                    return key == null || Convert.ToInt32(key.GetValue("AutoDoppler", 1)) != 0;
            }
            catch { return true; }
        }

        private static void SaveAutoDoppler(bool enabled)
        {
            try
            {
                using (RegistryKey key = Registry.CurrentUser.CreateSubKey(@"Software\TinyDoppler"))
                    if (key != null)
                        key.SetValue("AutoDoppler", enabled ? 1 : 0, RegistryValueKind.DWord);
            }
            catch { /* A read-only profile must not interrupt reception. */ }
        }

        public BridgeControlPanel(ISharpControl control, IqBridgeProcessor processor)
        {
            _control = control;
            _processor = processor;
            _doppler = new DopplerControlReader();
            AutoSize = true;
            Padding = new Padding(8);
            TableLayoutPanel layout = new TableLayoutPanel
            {
                Dock = DockStyle.Top,
                AutoSize = true,
                ColumnCount = 1,
                RowCount = 4
            };
            Controls.Add(layout);
            layout.Controls.Add(new Label
            {
                Text = "Tiny Doppler",
                AutoSize = true,
                Font = new Font(Font, FontStyle.Bold)
            });
            _enabled = new CheckBox
            {
                Text = TextFor("向接收器发送 I/Q", "Send I/Q to receiver", "受信機へI/Qを送る"),
                Checked = processor.Enabled,
                AutoSize = true
            };
            _enabled.CheckedChanged += delegate { processor.Enabled = _enabled.Checked; };
            layout.Controls.Add(_enabled);
            _autoDoppler = new CheckBox
            {
                Text = TextFor("自动多普勒调谐", "Automatic Doppler tuning", "自動ドップラー補正"),
                Checked = LoadAutoDoppler(),
                AutoSize = true
            };
            _autoDoppler.CheckedChanged += delegate { SaveAutoDoppler(_autoDoppler.Checked); };
            layout.Controls.Add(_autoDoppler);
            _status = new Label { AutoSize = true };
            layout.Controls.Add(_status);
            _timer = new Timer { Interval = 500 };
            _timer.Tick += delegate { UpdateStatus(); };
            _timer.Start();
            UpdateStatus();
        }

        private void UpdateStatus()
        {
            string bridge = _processor.Enabled
                ? TextFor("I/Q 已启用", "I/Q active", "I/Q有効")
                : TextFor("I/Q 已关闭", "I/Q off", "I/Qオフ");
            long targetHz;
            long correctionHz;
            if (!_autoDoppler.Checked)
            {
                _status.Text = bridge + "\r\n" +
                    TextFor("多普勒已关闭", "Doppler off", "ドップラー補正オフ");
            }
            else if (_doppler.TryRead(out targetHz, out correctionHz))
            {
                try
                {
                    if (Math.Abs(_control.Frequency - targetHz) >= 1)
                        _control.Frequency = targetHz;
                    _status.Text = string.Format(
                        "{0}\r\nDOPPLER {1:+0;-0;0} Hz  ->  {2:0.000000} MHz",
                        bridge, correctionHz, targetHz / 1000000.0);
                }
                catch
                {
                    _status.Text = bridge + "\r\n" +
                        TextFor("无法调谐", "Tuning unavailable", "同調できません");
                }
            }
            else
            {
                _status.Text = bridge + "\r\n" +
                    TextFor("等待跟踪器", "Waiting for tracker", "追跡待機中");
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                _timer.Dispose();
                _doppler.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
