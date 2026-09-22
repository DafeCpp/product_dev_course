using System.Text;

namespace Ski.Workstation.Application.Export;

public sealed class TxtResultExporter : IResultExporter
{
    public ResultExportFormat Format => ResultExportFormat.Txt;
    public string FileExtension => ".txt";

    public async Task ExportAsync(MeasurementExportDocument document, Stream destination, CancellationToken cancellationToken = default)
    {
        using var writer = new StreamWriter(destination, new UTF8Encoding(encoderShouldEmitUTF8Identifier: true), leaveOpen: true);
        await writer.WriteLineAsync($"Эксперимент\t{Clean(document.ExperimentName)}".AsMemory(), cancellationToken);
        await writer.WriteLineAsync($"Дата и время\t{document.RecordedAt:O}".AsMemory(), cancellationToken);
        await writer.WriteLineAsync(string.Join('\t', ExportTable.Headers).AsMemory(), cancellationToken);
        foreach (var row in ExportTable.GetRows(document))
        {
            cancellationToken.ThrowIfCancellationRequested();
            await writer.WriteLineAsync(string.Join('\t', row.Select(ExportTable.Number)).AsMemory(), cancellationToken);
        }

        await writer.FlushAsync(cancellationToken);
    }

    private static string Clean(string value) => value.Replace('\t', ' ').Replace('\r', ' ').Replace('\n', ' ');
}
