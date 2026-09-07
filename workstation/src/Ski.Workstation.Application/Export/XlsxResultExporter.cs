using System.IO.Compression;
using System.Security;
using System.Text;

namespace Ski.Workstation.Application.Export;

public sealed class XlsxResultExporter : IResultExporter
{
    public ResultExportFormat Format => ResultExportFormat.Xlsx;
    public string FileExtension => ".xlsx";

    public Task ExportAsync(MeasurementExportDocument document, Stream destination, CancellationToken cancellationToken = default)
    {
        using var archive = new ZipArchive(destination, ZipArchiveMode.Create, leaveOpen: true);
        Write(archive, "[Content_Types].xml", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/><Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/></Types>");
        Write(archive, "_rels/.rels", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>");
        Write(archive, "xl/workbook.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets><sheet name=\"Результаты\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>");
        Write(archive, "xl/_rels/workbook.xml.rels", "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/></Relationships>");

        var sheet = new StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>");
        AppendStrings(sheet, new[] { "Эксперимент", document.ExperimentName });
        AppendStrings(sheet, new[] { "Дата и время", document.RecordedAt.ToString("O") });
        AppendStrings(sheet, ExportTable.Headers);
        foreach (var row in ExportTable.GetRows(document))
        {
            cancellationToken.ThrowIfCancellationRequested();
            sheet.Append("<row>");
            foreach (var value in row) sheet.Append("<c><v>").Append(ExportTable.Number(value)).Append("</v></c>");
            sheet.Append("</row>");
        }
        sheet.Append("</sheetData></worksheet>");
        Write(archive, "xl/worksheets/sheet1.xml", sheet.ToString());
        return Task.CompletedTask;
    }

    private static void AppendStrings(StringBuilder xml, IEnumerable<string> values)
    {
        xml.Append("<row>");
        foreach (var value in values) xml.Append("<c t=\"inlineStr\"><is><t xml:space=\"preserve\">").Append(SecurityElement.Escape(value)).Append("</t></is></c>");
        xml.Append("</row>");
    }

    private static void Write(ZipArchive archive, string path, string content)
    {
        using var writer = new StreamWriter(archive.CreateEntry(path, CompressionLevel.Optimal).Open(), new UTF8Encoding(false));
        writer.Write(content);
    }
}
