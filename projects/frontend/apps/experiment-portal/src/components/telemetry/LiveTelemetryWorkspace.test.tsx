import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";
import LiveTelemetryWorkspace from "./LiveTelemetryWorkspace";

vi.mock("../TelemetryPanel", () => ({
  default: ({ title, onRemove }: { title: string; onRemove: () => void }) => (
    <div>
      {title}
      <button onClick={onRemove}>Удалить панель</button>
    </div>
  ),
}));
vi.mock("../LiveSensorPanel", () => ({
  default: () => <div>Последние значения</div>,
}));
vi.mock("../CaptureSessionTimeline", () => ({
  default: () => <div>Таймлайн</div>,
}));

describe("LiveTelemetryWorkspace", () => {
  it("renders panels and delegates panel removal", async () => {
    const user = userEvent.setup();
    const onRemove = vi.fn();
    render(
      <LiveTelemetryWorkspace
        panelIds={["one"]}
        sensors={[]}
        sensorsLoading={false}
        sensorsError={null}
        projectId="p1"
        sessions={[]}
        recentValues={{}}
        panelSizes={{}}
        containerWidth={0}
        containerRef={{ current: null }}
        draggingId={null}
        dragOverId={null}
        onAddHistorySession={vi.fn()}
        onRemove={onRemove}
        onMove={vi.fn()}
        onSizeChange={vi.fn()}
        onRecordReceived={vi.fn()}
        onDraggingChange={vi.fn()}
        onDragOverChange={vi.fn()}
      />,
    );
    expect(screen.getByText("Панель #1")).toBeInTheDocument();
    await user.click(screen.getByRole("button", { name: "Удалить панель" }));
    expect(onRemove).toHaveBeenCalledWith("one");
  });
});
