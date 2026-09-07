using System.Security;
using System.Text;

namespace Ski.Workstation.Application.Export;

/// <summary>Excel 2003 SpreadsheetML writer. Excel opens the resulting .xls without Office interop.</summary>
public sealed class XlsResultExporter : IResultExporter
{
    public ResultExportFormat Format => ResultExportFormat.Xls;
    public string FileExtension => ".xls";

    public async Task ExportAsync(MeasurementExportDocument document, Stream destination, CancellationToken cancellationToken = default)
    {
        using var writer = new StreamWriter(destination, new UTF8Encoding(true), leaveOpen: true);
        await writer.WriteAsync("<?xml version=\"1.0\"?><Workbook xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\" xmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\"><Worksheet ss:Name=\"Результаты\"><Table>");
        await StringRow(writer, new[] { "Эксперимент", document.ExperimentName }, cancellationToken);
        await StringRow(writer, new[] { "Дата и время", document.RecordedAt.ToString("O") }, cancellationToken);
        await StringRow(writer, ExportTable.Headers, cancellationToken);
        foreach (var row in ExportTable.GetRows(document))
        {
            cancellationToken.ThrowIfCancellationRequested();
            await writer.WriteAsync("<Row>");
            foreach (var value in row)
            {
                await writer.WriteAsync($"<Cell><Data ss:Type=\"Number\">{ExportTable.Number(value)}</Data></Cell>");
            }
            await writer.WriteAsync("</Row>");
        }
        await writer.WriteAsync("</Table></Worksheet></Workbook>");
        await writer.FlushAsync(cancellationToken);
    }

    private static async Task StringRow(TextWriter writer, IEnumerable<string> values, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        await writer.WriteAsync("<Row>");
        foreach (var value in values)
        {
            await writer.WriteAsync($"<Cell><Data ss:Type=\"String\">{SecurityElement.Escape(value)}</Data></Cell>");
        }
        await writer.WriteAsync("</Row>");
    }
}
