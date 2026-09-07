using System.Globalization;

namespace Ski.Workstation.Application.Export;

internal static class ExportTable
{
    public const double MpaPerKgfPerSquareMillimetre = 9.80665;

    public static readonly string[] Headers =
    {
        "Время, с",
        "Характеристика, МПа",
        "Характеристика, кгс/мм²",
        "Интегральная характеристика, МПа",
        "Интегральная характеристика, кгс/мм²",
        "Статическая составляющая, МПа",
        "Статическая составляющая, кгс/мм²",
    };

    public static IEnumerable<double[]> GetRows(MeasurementExportDocument document)
    {
        foreach (var row in document.Rows)
        {
            yield return new[]
            {
                row.TimeSeconds,
                row.StressMpa,
                ToKgfPerSquareMillimetre(row.StressMpa),
                row.IntegralMpa,
                ToKgfPerSquareMillimetre(row.IntegralMpa),
                row.StaticMpa,
                ToKgfPerSquareMillimetre(row.StaticMpa),
            };
        }
    }

    public static double ToKgfPerSquareMillimetre(double mpa) => mpa / MpaPerKgfPerSquareMillimetre;

    public static string Number(double value)
    {
        if (!double.IsFinite(value))
        {
            throw new InvalidDataException("Экспортируемые значения должны быть конечными числами.");
        }

        return value.ToString("G17", CultureInfo.InvariantCulture);
    }
}
