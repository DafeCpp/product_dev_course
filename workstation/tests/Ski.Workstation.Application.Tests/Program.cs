using System.IO.Compression;
using System.Text;
using Ski.Workstation.Application.Export;

var document = new MeasurementExportDocument(
    "Испытание & 1",
    new DateTimeOffset(2026, 9, 7, 12, 30, 0, TimeSpan.Zero),
    new[] { new MeasurementExportRow(0.25, 9.80665, 19.6133, -9.80665) });
var service = ResultExportService.CreateDefault();

await using (var output = new MemoryStream())
{
    await service.ExportAsync(document, ResultExportFormat.Txt, output);
    var text = Encoding.UTF8.GetString(output.ToArray());
    Assert(text.Contains("Интегральная характеристика, кгс/мм²"), "TXT содержит заголовок интегральной характеристики");
    Assert(text.Contains("9.80665\t1"), "TXT содержит значения в МПа и кгс/мм²");
}

await using (var output = new MemoryStream())
{
    await service.ExportAsync(document, ResultExportFormat.Xls, output);
    var xml = Encoding.UTF8.GetString(output.ToArray());
    Assert(xml.Contains("Испытание &amp; 1"), "XLS экранирует XML");
    Assert(xml.Contains("Статическая составляющая, МПа"), "XLS содержит статическую составляющую");
}

await using (var output = new MemoryStream())
{
    await service.ExportAsync(document, ResultExportFormat.Xlsx, output);
    output.Position = 0;
    using var archive = new ZipArchive(output, ZipArchiveMode.Read);
    var entry = archive.GetEntry("xl/worksheets/sheet1.xml");
    Assert(entry is not null, "XLSX содержит лист");
    using var reader = new StreamReader(entry!.Open());
    var xml = await reader.ReadToEndAsync();
    Assert(xml.Contains("Интегральная характеристика, МПа"), "XLSX содержит интегральную характеристику");
    Assert(xml.Contains("<v>-1</v>"), "XLSX конвертирует статическую составляющую в кгс/мм²");
}

Console.WriteLine("Все проверки экспорта успешно выполнены.");

static void Assert(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException($"Ошибка проверки: {message}");
}
