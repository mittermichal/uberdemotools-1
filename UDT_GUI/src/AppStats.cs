using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;


namespace Uber.DemoTools
{
    public class DemoStatsField
    {
        public string Key = "";
        public string Value = "";
        public UDT_DLL.udtStatsCompMode ComparisonMode = UDT_DLL.udtStatsCompMode.NeitherWins;
        public int FieldBitIndex = 0;
        public int IntegerValue = 0;
    }

    public class StatsInfoGroup
    {
        public string Name = "N/A"; // What should be used as a column header.
        public readonly List<DemoStatsField> Fields = new List<DemoStatsField>();
        public int Index = 0; // Used for sorting the groups...
        public int TeamIndex = -1; // Only relevant to players.
    }

    public class DemoStatsInfo
    {
        public void AddGenericField(string key, string value)
        {
            if(value != null)
            {
                GenericFields.Add(new DemoStatsField { Key = key, Value = value });
            }
        }

        public void AddGenericField(string key, IntPtr stringValue)
        {
            if(stringValue != IntPtr.Zero)
            {
                GenericFields.Add(new DemoStatsField { Key = key, Value = UDT_DLL.SafeGetUTF8String(stringValue) });
            }
        }

        public readonly List<DemoStatsField> GenericFields = new List<DemoStatsField>();
        public readonly List<StatsInfoGroup> TeamStats = new List<StatsInfoGroup>();
        public readonly List<StatsInfoGroup> PlayerStats = new List<StatsInfoGroup>();
    }

    public class StatsComponent : AppComponent
    {
        private DemoInfoListView _matchInfoListView;
        private DemoInfoListView _teamStatsListView;
        private DemoInfoListView _playerStatsListView;
        private FrameworkElement _noStatsPanel;
        private FrameworkElement _statsPanel;
        private FrameworkElement _matchInfoPanel;
        private FrameworkElement _teamStatsPanel;
        private FrameworkElement _playerStatsPanel;
        private ScrollViewer _scrollViewer;
        private int _selectedStatsIndex = 0;
        private App _app;

        private class TeamStatsDisplayInfo
        {
            public TeamStatsDisplayInfo(string key, string red, string blue)
            {
                Key = key;
                Red = red;
                Blue = blue;
                Bold = new bool[] { false, false };
            }

            public string Key { get; set; }
            public string Red { get; set; }
            public string Blue { get; set; }
            public bool[] Bold { get; set; }
            public FontWeight RedFontWeight { get { return Bold[0] ? FontWeights.Bold : FontWeights.Normal; } }
            public FontWeight BlueFontWeight { get { return Bold[1] ? FontWeights.Bold : FontWeights.Normal; } }
        }

        private class PlayerStatsDisplayInfo
        {
            public PlayerStatsDisplayInfo(string key, int playerCount)
            {
                Key = key;
                Values = new string[playerCount];
                ValueFontWeights = new FontWeight[playerCount];
                for(var i = 0; i < playerCount; ++i)
                {
                    Values[i] = "";
                    ValueFontWeights[i] = FontWeights.Normal;
                }
            }

            public string Key { get; set; }
            public string[] Values { get; set; }
            public FontWeight[] ValueFontWeights { get; set; }
        }

        private const int KeyColumnWidth = 150;

        public FrameworkElement RootControl { get; private set; }
        public List<DemoInfoListView> AllListViews { get { return new List<DemoInfoListView> { _matchInfoListView, _teamStatsListView, _playerStatsListView }; } }
        public List<DemoInfoListView> InfoListViews { get { return new List<DemoInfoListView> { _matchInfoListView, _teamStatsListView, _playerStatsListView }; } }
        public ComponentType Type { get { return ComponentType.Stats; } }
        public bool MultiDemoMode { get { return false; } }

        public StatsComponent(App app)
        {
            _app = app;
            RootControl = CreateTab();
        }

        public void PopulateViews(DemoInfo demoInfo)
        {
            var showStats = demoInfo.Analyzed;
            _statsPanel.Visibility = showStats ? Visibility.Visible : Visibility.Collapsed;
            _noStatsPanel.Visibility = showStats ? Visibility.Collapsed : Visibility.Visible;
            _matchInfoListView.Items.Clear();
            _teamStatsListView.ItemsSource = new ObservableCollection<TeamStatsDisplayInfo>();
            _playerStatsListView.ItemsSource = new ObservableCollection<PlayerStatsDisplayInfo>();
            if(!showStats)
            {
                ShowMatchInfo(false);
                ShowTeamStats(false);
                ShowPlayerStats(false);
                return;
            }

            if(demoInfo.MatchStats.Count == 0)
            {
                ShowMatchInfo(false);
                ShowTeamStats(false);
                ShowPlayerStats(false);
                return;
            }

            if(_selectedStatsIndex < 0 || _selectedStatsIndex >= demoInfo.MatchStats.Count)
            {
                _selectedStatsIndex = 0;
            }

            var stats = demoInfo.MatchStats[_selectedStatsIndex];
            ShowMatchInfo(stats.GenericFields.Count > 0);
            ShowTeamStats(stats.TeamStats.Count > 0);
            ShowPlayerStats(stats.PlayerStats.Count > 0);

            foreach(var field in stats.GenericFields)
            {
                _matchInfoListView.Items.Add(new string[] { field.Key, field.Value });
            }
            
            if(stats.TeamStats.Count == 2 &&
               stats.TeamStats[0].Fields.Count == stats.TeamStats[1].Fields.Count)
            {
                var items = new ObservableCollection<TeamStatsDisplayInfo>();
                var fieldCount = stats.TeamStats[0].Fields.Count;
                for(var i = 0; i < fieldCount; ++i)
                {
                    var field0 = stats.TeamStats[0].Fields[i];
                    var field1 = stats.TeamStats[1].Fields[i];

                    var index = -1;
                    if(field0.ComparisonMode == UDT_DLL.udtStatsCompMode.BiggerWins)
                    {
                        index = field0.IntegerValue > field1.IntegerValue ? 0 : index;
                        index = field0.IntegerValue < field1.IntegerValue ? 1 : index;
                    }
                    else if(field0.ComparisonMode == UDT_DLL.udtStatsCompMode.SmallerWins)
                    {
                        index = field0.IntegerValue < field1.IntegerValue ? 0 : index;
                        index = field0.IntegerValue > field1.IntegerValue ? 1 : index;
                    }

                    var info = new TeamStatsDisplayInfo(field0.Key, field0.Value, field1.Value);
                    if(index != -1)
                    {
                        info.Bold[index] = true;
                    }

                    items.Add(info);
                }

                _teamStatsListView.ItemsSource = items;
            }

            // The annoyance we have to deal with is that we don't necessarily have the same fields
            // defined for all players. Also, the order isn't guaranteed.
            var playerCount = stats.PlayerStats.Count;
            var playerItems = new ObservableCollection<PlayerStatsDisplayInfo>();
            for(var i = 0; i < UDT_DLL.UDT_PLAYER_STATS_FIELD_COUNT; ++i)
            {
                DemoStatsField field = null;
                for(var j = 0; j < playerCount; ++j)
                {
                    field = stats.PlayerStats[j].Fields.Find(f => f.FieldBitIndex == i);
                    if(field != null)
                    {
                        break;
                    }
                }
                if(field == null)
                {
                    continue;
                }

                var info = new PlayerStatsDisplayInfo(field.Key, playerCount);

                var boldIndex = -1;
                var extremeValue = field.ComparisonMode == UDT_DLL.udtStatsCompMode.BiggerWins ? int.MinValue : int.MaxValue;
                for(var j = 0; j < playerCount; ++j)
                {
                    field = stats.PlayerStats[j].Fields.Find(f => f.FieldBitIndex == i);
                    if(field == null)
                    {
                        continue;
                    }

                    info.Values[j] = field.Value;

                    var currValue = field.IntegerValue;
                    if(field.ComparisonMode != UDT_DLL.udtStatsCompMode.NeitherWins && currValue == extremeValue && j > 0)
                    {
                        boldIndex = -1;
                    }
                    else if(field.ComparisonMode == UDT_DLL.udtStatsCompMode.BiggerWins && (currValue > extremeValue || j == 0))
                    {
                        extremeValue = currValue;
                        boldIndex = j;
                    }
                    else if(field.ComparisonMode == UDT_DLL.udtStatsCompMode.SmallerWins && (currValue < extremeValue || j == 0))
                    {
                        extremeValue = currValue;
                        boldIndex = j;
                    }                    
                }

                if(boldIndex != -1)
                {
                    info.ValueFontWeights[boldIndex] = FontWeights.Bold;
                }

                playerItems.Add(info);
            }
            _playerStatsListView.ItemsSource = playerItems;

            // Update the player columns while preserving the key column.
            var gridView = _playerStatsListView.View as GridView;
            var column0 = gridView.Columns[0];
            gridView.Columns.Clear();
            gridView.Columns.Add(column0);
            for(var i = 0; i < playerCount; ++i)
            {
                var template = new DataTemplate();
                var factory = new FrameworkElementFactory(typeof(TextBlock));
                factory.SetBinding(TextBlock.TextProperty, new Binding(string.Format("Values[{0}]", i)));
                factory.SetBinding(TextBlock.FontWeightProperty, new Binding(string.Format("ValueFontWeights[{0}]", i)));
                template.VisualTree = factory;
                var column = new GridViewColumn();
                var header = new GridViewColumnHeader();
                header.Content = stats.PlayerStats[i].Name;
                header.Tag = "Value";
                column.Header = header;
                column.Width = KeyColumnWidth;
                column.CellTemplate = template;
                gridView.Columns.Add(column);
            }
        }

        public void SaveToConfigObject(UdtConfig config)
        {
            // Nothing to do.
        }

        public void SaveToConfigObject(UdtPrivateConfig config)
        {
            // Nothing to do.
        }

        private FrameworkElement CreateTab()
        {
            var noDataTextBlock = new TextBlock();
            _noStatsPanel = noDataTextBlock;
            noDataTextBlock.HorizontalAlignment = HorizontalAlignment.Center;
            noDataTextBlock.VerticalAlignment = VerticalAlignment.Center;
            noDataTextBlock.Text = "This demo was not analyzed.";
            noDataTextBlock.Visibility = Visibility.Collapsed;

            var matchInfoListView = CreateMatchInfoListView();
            var teamStatsListView = CreateTeamStatsListView();
            var playerStatsListView = CreatePlayerStatsListView();
            FixListViewMouseWheelHandling(matchInfoListView);
            FixListViewMouseWheelHandling(teamStatsListView);
            FixListViewMouseWheelHandling(playerStatsListView);
            _matchInfoListView = matchInfoListView;
            _teamStatsListView = teamStatsListView;
            _playerStatsListView = playerStatsListView;

            var matchInfoGroupBox = new GroupBox();
            _matchInfoPanel = matchInfoGroupBox;
            matchInfoGroupBox.HorizontalAlignment = HorizontalAlignment.Stretch;
            matchInfoGroupBox.VerticalAlignment = VerticalAlignment.Stretch;
            matchInfoGroupBox.Margin = new Thickness(5);
            matchInfoGroupBox.Header = "Match Info";
            matchInfoGroupBox.Content = matchInfoListView;

            var teamStatsGroupBox = new GroupBox();
            _teamStatsPanel = teamStatsGroupBox;
            teamStatsGroupBox.HorizontalAlignment = HorizontalAlignment.Stretch;
            teamStatsGroupBox.VerticalAlignment = VerticalAlignment.Stretch;
            teamStatsGroupBox.Margin = new Thickness(5);
            teamStatsGroupBox.Header = "Team Scores and Stats";
            teamStatsGroupBox.Content = teamStatsListView;

            var playerStatsGroupBox = new GroupBox();
            _playerStatsPanel = playerStatsGroupBox;
            playerStatsGroupBox.HorizontalAlignment = HorizontalAlignment.Stretch;
            playerStatsGroupBox.VerticalAlignment = VerticalAlignment.Stretch;
            playerStatsGroupBox.Margin = new Thickness(5);
            playerStatsGroupBox.Header = "Player Scores and Stats";
            playerStatsGroupBox.Content = playerStatsListView;

            var statsPanel = new StackPanel();
            statsPanel.HorizontalAlignment = HorizontalAlignment.Stretch;
            statsPanel.VerticalAlignment = VerticalAlignment.Stretch;
            statsPanel.Margin = new Thickness(5);
            statsPanel.Orientation = Orientation.Vertical;
            statsPanel.Children.Add(matchInfoGroupBox);
            statsPanel.Children.Add(teamStatsGroupBox);
            statsPanel.Children.Add(playerStatsGroupBox);

            var rootPanel = new StackPanel();
            _statsPanel = statsPanel;
            rootPanel.HorizontalAlignment = HorizontalAlignment.Stretch;
            rootPanel.VerticalAlignment = VerticalAlignment.Stretch;
            rootPanel.Margin = new Thickness(5);
            rootPanel.Orientation = Orientation.Vertical;
            rootPanel.Children.Add(_noStatsPanel);
            rootPanel.Children.Add(_statsPanel);

            var scrollViewer = new ScrollViewer();
            _scrollViewer = scrollViewer;
            scrollViewer.HorizontalAlignment = HorizontalAlignment.Stretch;
            scrollViewer.VerticalAlignment = VerticalAlignment.Stretch;
            scrollViewer.HorizontalScrollBarVisibility = ScrollBarVisibility.Auto;
            scrollViewer.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
            scrollViewer.Content = rootPanel;

            return scrollViewer; 
        }

        private DemoInfoListView CreateMatchInfoListView()
        {
            var columnKey = new GridViewColumn();
            var headerKey = new GridViewColumnHeader();
            headerKey.Content = "Key";
            headerKey.Tag = "Key";
            columnKey.Header = headerKey;
            columnKey.Width = KeyColumnWidth;
            columnKey.DisplayMemberBinding = new Binding("[0]");

            var columnValue = new GridViewColumn();
            var headerValue = new GridViewColumnHeader();
            headerValue.Content = "Value";
            headerValue.Tag = "Value";
            columnValue.Header = headerValue;
            columnValue.Width = 400;
            columnValue.DisplayMemberBinding = new Binding("[1]");

            var gridView = new GridView();
            gridView.AllowsColumnReorder = false;
            gridView.Columns.Add(columnKey);
            gridView.Columns.Add(columnValue);

            var listView = new DemoInfoListView();
            listView.HorizontalAlignment = HorizontalAlignment.Stretch;
            listView.VerticalAlignment = VerticalAlignment.Stretch;
            listView.Margin = new Thickness(5);
            listView.View = gridView;
            listView.SelectionMode = SelectionMode.Extended;

            return listView;
        }

        private static DataTemplate CreateCellTemplate(string textBinding, string fontWeightBinding)
        {
            var template = new DataTemplate();
            var factory = new FrameworkElementFactory(typeof(TextBlock));
            factory.SetBinding(TextBlock.TextProperty, new Binding(textBinding));
            factory.SetBinding(TextBlock.FontWeightProperty, new Binding(fontWeightBinding));
            template.VisualTree = factory;

            return template;
        }

        private DemoInfoListView CreateTeamStatsListView()
        {
            var columnKey = new GridViewColumn();
            var headerKey = new GridViewColumnHeader();
            headerKey.Content = "Key";
            headerKey.Tag = "Key";
            columnKey.Header = headerKey;
            columnKey.Width = KeyColumnWidth;
            columnKey.DisplayMemberBinding = new Binding("Key");

            var columnRed = new GridViewColumn();
            var headerRed = new GridViewColumnHeader();
            headerRed.Content = "Red";
            headerRed.Tag = "Red";
            columnRed.Header = headerRed;
            columnRed.Width = 200;
            columnRed.CellTemplate = CreateCellTemplate("Red", "RedFontWeight");

            var columnBlue = new GridViewColumn();
            var headerBlue = new GridViewColumnHeader();
            headerBlue.Content = "Blue";
            headerBlue.Tag = "Blue";
            columnBlue.Header = headerBlue;
            columnBlue.Width = 200;
            columnBlue.CellTemplate = CreateCellTemplate("Blue", "BlueFontWeight");

            var gridView = new GridView();
            gridView.AllowsColumnReorder = false;
            gridView.Columns.Add(columnKey);
            gridView.Columns.Add(columnRed);
            gridView.Columns.Add(columnBlue);

            var listView = new DemoInfoListView();
            listView.HorizontalAlignment = HorizontalAlignment.Stretch;
            listView.VerticalAlignment = VerticalAlignment.Stretch;
            listView.Margin = new Thickness(5);
            listView.View = gridView;
            listView.SelectionMode = SelectionMode.Extended;

            return listView;
        }

        private DemoInfoListView CreatePlayerStatsListView()
        {
            var columnKey = new GridViewColumn();
            var headerKey = new GridViewColumnHeader();
            headerKey.Content = "Key";
            headerKey.Tag = "Key";
            columnKey.Header = headerKey;
            columnKey.Width = KeyColumnWidth;
            columnKey.DisplayMemberBinding = new Binding("Key");

            var gridView = new GridView();
            gridView.AllowsColumnReorder = true;
            gridView.Columns.Add(columnKey);

            var listView = new DemoInfoListView();
            listView.HorizontalAlignment = HorizontalAlignment.Stretch;
            listView.VerticalAlignment = VerticalAlignment.Stretch;
            listView.Margin = new Thickness(5);
            listView.View = gridView;
            listView.SelectionMode = SelectionMode.Extended;

            return listView;
        }
        
        private void FixListViewMouseWheelHandling(ListView listView)
        {
            listView.AddHandler(ListView.MouseWheelEvent, new RoutedEventHandler(MouseWheelHandler), true);
        }

        private void MouseWheelHandler(object sender, RoutedEventArgs args)
        {
            if(_scrollViewer == null)
            {
                return;
            }

            var info = args as MouseWheelEventArgs;
            var posDelta = (double)info.Delta;
            var curPos = _scrollViewer.VerticalOffset;
            _scrollViewer.ScrollToVerticalOffset(curPos - posDelta);
        }
        
        private void ShowMatchInfo(bool show)
        {
            _matchInfoPanel.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        }

        private void ShowTeamStats(bool show)
        {
            _teamStatsPanel.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        }

        private void ShowPlayerStats(bool show)
        {
            _playerStatsPanel.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        }
    }
}