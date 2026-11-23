using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

internal sealed class Radar : FrameworkElement
{
    private sealed class Echo
    {
        internal Point Position;
        internal double Detected = -1;
    }

    private const double Turn = Math.PI * 2, SweepTime = 4.8, EchoLife = 3.2;
    private static readonly Brush Face = Solid(235, 22, 34, 28);
    private static readonly Pen Ring = Stroke(Solid(155, 104, 141, 110), 1);
    private static readonly Pen Rim = Stroke(Solid(232, 127, 167, 128), 1.2);
    private static readonly Brush TargetGlow = Solid(255, 209, 245, 163);
    private Pen signal;
    private Brush cursorGlow;
    private Brush[] trail;

    private readonly DispatcherTimer timer = new DispatcherTimer(DispatcherPriority.Render)
        { Interval = TimeSpan.FromMilliseconds(25) };
    private readonly Stopwatch clock = new Stopwatch();
    private readonly Random random = new Random();
    private readonly List<Echo> contacts = new List<Echo>(6);
    private Echo pointer;
    private double angle, time, nextContact = 0.75;

    internal Radar()
    {
        Focusable = false;
        Accent = Color.FromRgb(255, 183, 125);
        timer.Tick += (sender, args) => Sweep();
    }

    internal Color Accent
    {
        set
        {
            cursorGlow = Solid(255, value.R, value.G, value.B);
            signal = Stroke(cursorGlow, 1.6);
            trail = Enumerable.Range(1, 25)
                .Select(step => Solid((byte)(100 * Math.Pow(step / 25.0, 1.8)), value.R, value.G, value.B)).ToArray();
            InvalidateVisual();
        }
    }

    internal bool Active
    {
        set
        {
            if (value) { clock.Start(); timer.Start(); }
            else { timer.Stop(); clock.Stop(); }
        }
    }

    private Point Center { get { return new Point(ActualWidth / 2, ActualHeight / 2); } }
    private double Radius { get { return Math.Max(0, Math.Min(ActualWidth, ActualHeight) / 2 - 8); } }

    private static double Wrap(double value)
    {
        value %= Turn;
        return value < 0 ? value + Turn : value;
    }

    private static bool Crossed(double from, double span, Point point)
    {
        return Wrap(Math.Atan2(point.X, -point.Y) - from) <= span;
    }

    private void Sweep()
    {
        double now = clock.Elapsed.TotalSeconds;
        double previous = angle;
        double span = Math.Min(Turn, (now - time) * Turn / SweepTime);
        time = now;
        angle = Wrap(previous + span);
        contacts.RemoveAll(echo => echo.Detected >= 0 && now - echo.Detected >= EchoLife);
        if (now >= nextContact && contacts.Count < 6)
        {
            double heading = random.NextDouble() * Turn;
            double range = Math.Sqrt(0.07 + random.NextDouble() * 0.79);
            contacts.Add(new Echo { Position = PointAt(new Point(), range, heading) });
            nextContact = now + 1.25 + random.NextDouble() * 2.1;
        }
        foreach (var echo in contacts)
            if (echo.Detected < 0 && Crossed(previous, span, echo.Position)) echo.Detected = now;

        double radius = Radius;
        if (radius > 0 && IsMouseOver)
        {
            Point position = Mouse.GetPosition(this);
            Vector offset = (position - Center) / radius;
            var target = new Point(offset.X, offset.Y);
            if (offset.LengthSquared <= 1 && Crossed(previous, span, target))
                pointer = new Echo { Position = target, Detected = now };
        }
        InvalidateVisual();
    }

    protected override void OnRender(DrawingContext dc)
    {
        base.OnRender(dc);
        Point center = Center;
        double radius = Radius;
        if (radius <= 0) return;
        dc.DrawEllipse(Face, null, center, radius, radius);
        double breadth = Turn / 6;
        Point last = PointAt(center, radius, angle - breadth);
        for (int i = 0; i < trail.Length; i++)
        {
            Point next = PointAt(center, radius, angle - breadth * (1 - (i + 1.0) / trail.Length));
            var sector = new StreamGeometry();
            using (var path = sector.Open())
            {
                path.BeginFigure(center, true, true);
                path.LineTo(last, true, false);
                path.ArcTo(next, new Size(radius, radius), 0, false, SweepDirection.Clockwise, true, false);
            }
            dc.DrawGeometry(trail[i], null, sector);
            last = next;
        }
        for (int i = 1; i <= 4; i++)
            dc.DrawEllipse(null, i == 4 ? Rim : Ring, center, radius * i / 4, radius * i / 4);
        for (int tick = 0; tick < 24; tick++)
        {
            double theta = Turn * tick / 24;
            dc.DrawLine(Ring, PointAt(center, radius - (tick % 6 == 0 ? 9 : 4), theta), PointAt(center, radius, theta));
        }
        dc.DrawLine(Ring, new Point(center.X - radius, center.Y), new Point(center.X + radius, center.Y));
        dc.DrawLine(Ring, new Point(center.X, center.Y - radius), new Point(center.X, center.Y + radius));
        dc.DrawLine(signal, center, PointAt(center, radius, angle));
        foreach (var echo in contacts) DrawEcho(dc, echo, center, radius, false);
        if (pointer != null) DrawEcho(dc, pointer, center, radius, true);
    }

    private void DrawEcho(DrawingContext dc, Echo echo, Point center, double radius, bool cursor)
    {
        double age = time - echo.Detected;
        if (echo.Detected < 0 || age >= EchoLife) return;
        double intensity = Math.Pow(1 - age / EchoLife, 1.4);
        var point = center + new Vector(echo.Position.X * radius, echo.Position.Y * radius);
        Brush color = cursor ? cursorGlow : TargetGlow;
        dc.PushOpacity(intensity * 0.28);
        dc.DrawEllipse(color, null, point, 6 + age, 6 + age);
        dc.Pop();
        dc.PushOpacity(intensity);
        dc.DrawEllipse(color, null, point, cursor ? 3 : 2, cursor ? 3 : 2);
        if (cursor)
        {
            dc.PushTransform(new RotateTransform(45, point.X, point.Y));
            dc.DrawRectangle(null, Stroke(color, 1), new Rect(point.X - 5, point.Y - 5, 10, 10));
            dc.Pop();
        }
        dc.Pop();
    }

    private static Point PointAt(Point center, double radius, double theta)
    {
        return new Point(center.X + Math.Sin(theta) * radius, center.Y - Math.Cos(theta) * radius);
    }

    private static Brush Solid(byte alpha, byte red, byte green, byte blue)
    {
        var brush = new SolidColorBrush(Color.FromArgb(alpha, red, green, blue));
        brush.Freeze();
        return brush;
    }

    private static Pen Stroke(Brush color, double width)
    {
        var pen = new Pen(color, width);
        pen.Freeze();
        return pen;
    }
}
