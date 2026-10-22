namespace Ski.Workstation.Application.Export;

/// <summary>Данные результата постсеансной обработки, подготовленные для экспорта.</summary>
public sealed record MeasurementExportDocument(
    string ExperimentName,
    DateTimeOffset RecordedAt,
    IReadOnlyList<MeasurementExportRow> Rows);

/// <param name="StressMpa">Полная измеренная характеристика.</param>
/// <param name="IntegralMpa">Интегральная характеристика.</param>
/// <param name="StaticMpa">Статическая составляющая.</param>
public sealed record MeasurementExportRow(
    double TimeSeconds,
    double StressMpa,
    double IntegralMpa,
    double StaticMpa);
