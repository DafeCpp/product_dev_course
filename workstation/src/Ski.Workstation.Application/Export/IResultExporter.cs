namespace Ski.Workstation.Application.Export;

public interface IResultExporter
{
    ResultExportFormat Format { get; }
    string FileExtension { get; }
    Task ExportAsync(MeasurementExportDocument document, Stream destination, CancellationToken cancellationToken = default);
}
