namespace Ski.Workstation.Application.Export;

public sealed class ResultExportService
{
    private readonly IReadOnlyDictionary<ResultExportFormat, IResultExporter> exporters;

    public ResultExportService(IEnumerable<IResultExporter> exporters)
    {
        this.exporters = exporters.ToDictionary(exporter => exporter.Format);
    }

    public static ResultExportService CreateDefault() => new IResultExporter[]
    {
        new TxtResultExporter(),
        new XlsResultExporter(),
        new XlsxResultExporter(),
    }.ToExportService();

    public async Task ExportAsync(
        MeasurementExportDocument document,
        ResultExportFormat format,
        Stream destination,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(destination);
        if (!destination.CanWrite)
        {
            throw new ArgumentException("Поток экспорта недоступен для записи.", nameof(destination));
        }

        if (!exporters.TryGetValue(format, out var exporter))
        {
            throw new NotSupportedException($"Формат экспорта {format} не поддерживается.");
        }

        await exporter.ExportAsync(document, destination, cancellationToken).ConfigureAwait(false);
    }
}

internal static class ResultExporterExtensions
{
    public static ResultExportService ToExportService(this IEnumerable<IResultExporter> exporters) => new(exporters);
}
