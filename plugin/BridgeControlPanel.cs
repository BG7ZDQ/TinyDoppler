using System;
using System.Drawing;
using System.Windows.Forms;
using SDRSharp.Common;

namespace SDRSharp.AstroSeriesBridge
{
    internal sealed class BridgeControlPanel : UserControl
    {
        private readonly IqBridgeProcessor _processor;
        private readonly ISharpControl _control;
        private readonly DopplerControlReader _doppler;
        private readonly ComboBox _language;
        private readonly CheckBox _enabled;
        private readonly CheckBox _autoDoppler;
        private readonly Label _status;
        private readonly Timer _timer;
        private string _languageCode = "en";

        private string L(string chinese, string english, string japanese)
        {
            return _languageCode == "zh" ? chinese
                 : _languageCode == "ja" ? japanese : english;
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
                RowCount = 5
            };
            Controls.Add(layout);
            layout.Controls.Add(new Label
            {
                Text = "Tiny Doppler",
                AutoSize = true,
                Font = new Font(Font, FontStyle.Bold)
            });
            _language = new ComboBox
            {
                DropDownStyle = ComboBoxStyle.DropDownList,
                AutoSize = true,
                Width = 120
            };
            _language.Items.AddRange(new object[] { "English", "中文", "日本語" });
            _language.SelectedIndex = 0; // English by default for every UI culture.
            layout.Controls.Add(_language);
            _enabled = new CheckBox
            {
                Text = L("启用共享内存桥接", "Enable shared-memory bridge", "共有メモリブリッジを有効化"),
                Checked = processor.Enabled,
                AutoSize = true
            };
            _enabled.CheckedChanged += delegate {
                processor.Enabled = _enabled.Checked;
                _enabled.Checked = processor.Enabled;
                UpdateStatus();
            };
            layout.Controls.Add(_enabled);
            _autoDoppler = new CheckBox
            {
                Text = L("自动多普勒调谐", "Automatic Doppler tuning", "自動ドップラー補正"),
                Checked = true,
                AutoSize = true
            };
            layout.Controls.Add(_autoDoppler);
            _language.SelectedIndexChanged += delegate {
                _languageCode = _language.SelectedIndex == 1 ? "zh"
                               : _language.SelectedIndex == 2 ? "ja" : "en";
                _enabled.Text = L("启用共享内存桥接", "Enable shared-memory bridge",
                                  "共有メモリブリッジを有効化");
                _autoDoppler.Text = L("自动多普勒调谐", "Automatic Doppler tuning",
                                      "自動ドップラー補正");
                UpdateStatus();
            };
            _status = new Label { AutoSize = true };
            layout.Controls.Add(_status);
            _timer = new Timer { Interval = 500 };
            _timer.Tick += delegate { UpdateStatus(); };
            _timer.Start();
            UpdateStatus();
        }

        private void UpdateStatus()
        {
            _autoDoppler.Enabled = _processor.Enabled;
            if (!_processor.Enabled)
            {
                _status.Text = _processor.StartError ??
                    L("桥接已停止", "Bridge stopped", "ブリッジ停止中");
                return; // A rejected/disabled bridge must never control tuning.
            }
            string bridge = _processor.Enabled
                ? L("I/Q 已启用", "I/Q active", "I/Q有効")
                : L("I/Q 已关闭", "I/Q off", "I/Qオフ");
            long targetHz;
            long correctionHz;
            if (!_autoDoppler.Checked)
            {
                _status.Text = bridge + "\r\n" +
                    L("多普勒已关闭", "Doppler off", "ドップラー補正オフ");
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
                        L("无法调谐", "Tuning unavailable", "同調できません");
                }
            }
            else
            {
                _status.Text = bridge + "\r\n" +
                    L("等待跟踪器", "Waiting for tracker", "追跡待機中");
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
