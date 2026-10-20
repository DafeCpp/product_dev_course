import { createEvent, fireEvent, render, screen } from "@testing-library/react";
import type { ComponentProps, DragEvent } from "react";
import { describe, expect, it, vi } from "vitest";
import LiveTelemetryWorkspace from "./LiveTelemetryWorkspace";
import type { CaptureSession, Sensor } from "../../types";

vi.mock("../TelemetryPanel", () => ({
  default: ({
    title,
    sensorsError,
    onSizeChange,
    dragHandleProps,
  }: {
    title: string;
    sensorsError: string | null;
    onSizeChange: (size: { width: number; height: number }) => void;
    dragHandleProps: {
      onDragStart: (event: DragEvent<HTMLElement>) => void;
      onDragEnd: () => void;
    };
  }) => (
    <div data-testid="panel">
      <span>{title}</span>
      <span data-testid="panel-error">{sensorsError ?? "no-error"}</span>
      <button onClick={() => onSizeChange({ width: 10, height: 20 })}>
        resize
      </button>
      <span
        data-testid="drag-handle"
        draggable
        onDragStart={dragHandleProps.onDragStart}
        onDragEnd={dragHandleProps.onDragEnd}
      >
        handle
      </span>
    </div>
  ),
}));
vi.mock("../LiveSensorPanel", () => ({
  default: () => <div>LiveSensorPanelMock</div>,
}));
vi.mock("../CaptureSessionTimeline", () => ({
  default: ({ onSessionClick }: { onSessionClick: (id: string) => void }) => (
    <button onClick={() => onSessionClick("s1")}>Таймлайн</button>
  ),
}));

type Props = ComponentProps<typeof LiveTelemetryWorkspace>;

function makeProps(overrides: Partial<Props> = {}): Props {
  return {
    panelIds: ["one", "two"],
    sensors: [],
    sensorsLoading: false,
    sensorsError: null,
    projectId: "p1",
    sessions: [],
    recentValues: {},
    panelSizes: {},
    containerWidth: 0,
    containerRef: { current: null },
    draggingId: null,
    dragOverId: null,
    onAddHistorySession: vi.fn(),
    onRemove: vi.fn(),
    onMove: vi.fn(),
    onSizeChange: vi.fn(),
    onRecordReceived: vi.fn(),
    onDraggingChange: vi.fn(),
    onDragOverChange: vi.fn(),
    ...overrides,
  };
}

const sensor = { id: "sen1", name: "S1" } as Sensor;
const session = { id: "s1", ordinal_number: 1 } as CaptureSession;

function items(container: HTMLElement) {
  return Array.from(
    container.querySelectorAll<HTMLElement>(".telemetry-view__panel-item"),
  );
}

describe("LiveTelemetryWorkspace branches", () => {
  it("shows empty state when there are no panels", () => {
    render(<LiveTelemetryWorkspace {...makeProps({ panelIds: [] })} />);
    expect(
      screen.getByText("Добавьте панель, чтобы начать просмотр графиков."),
    ).toBeInTheDocument();
  });

  it("uses project name, string error, timeline and sensor panel", () => {
    const props = makeProps({
      projectName: "Proj",
      sensorsError: "boom",
      sessions: [session],
      sensors: [sensor],
    });
    render(<LiveTelemetryWorkspace {...props} />);
    expect(screen.getByText("Панель: Proj #1")).toBeInTheDocument();
    expect(screen.getAllByTestId("panel-error")[0]).toHaveTextContent("boom");
    expect(screen.getByText("LiveSensorPanelMock")).toBeInTheDocument();
    fireEvent.click(screen.getByText("Таймлайн"));
    expect(props.onAddHistorySession).toHaveBeenCalledWith("s1");
  });

  it("uses Error message and hides sensor panel without project", () => {
    render(
      <LiveTelemetryWorkspace
        {...makeProps({
          sensorsError: new Error("err-obj"),
          projectId: "",
          sensors: [sensor],
        })}
      />,
    );
    expect(screen.getAllByTestId("panel-error")[0]).toHaveTextContent(
      "err-obj",
    );
    expect(screen.queryByText("LiveSensorPanelMock")).not.toBeInTheDocument();
  });

  it("applies dragging/over/wide classes", () => {
    const { container } = render(
      <LiveTelemetryWorkspace
        {...makeProps({
          draggingId: "one",
          dragOverId: "two",
          containerWidth: 100,
          panelSizes: {
            one: { width: 80, height: 10 },
            two: { width: 20, height: 10 },
          },
        })}
      />,
    );
    const [one, two] = items(container);
    expect(one).toHaveClass("telemetry-view__panel-item--dragging");
    expect(one).toHaveClass("telemetry-view__panel-item--full");
    expect(two).toHaveClass("telemetry-view__panel-item--over");
    expect(two).not.toHaveClass("telemetry-view__panel-item--full");
  });

  it("handles drag over only for a different dragging panel", () => {
    const props = makeProps({ draggingId: "one" });
    const { container } = render(<LiveTelemetryWorkspace {...props} />);
    const [one, two] = items(container);
    fireEvent.dragOver(one);
    expect(props.onDragOverChange).not.toHaveBeenCalled();
    const ev = createEvent.dragOver(two);
    Object.defineProperty(ev, "dataTransfer", { value: { dropEffect: "" } });
    fireEvent(two, ev);
    expect(props.onDragOverChange).toHaveBeenCalledWith("two");
  });

  it("ignores drag over when nothing is dragged", () => {
    const props = makeProps();
    const { container } = render(<LiveTelemetryWorkspace {...props} />);
    fireEvent.dragOver(items(container)[1]);
    expect(props.onDragOverChange).not.toHaveBeenCalled();
  });

  it("handles drag leave inside and outside", () => {
    const props = makeProps();
    const { container } = render(<LiveTelemetryWorkspace {...props} />);
    const [one] = items(container);
    const leave = (related: EventTarget) => {
      const ev = createEvent.dragLeave(one);
      Object.defineProperty(ev, "relatedTarget", { value: related });
      fireEvent(one, ev);
    };
    leave(screen.getAllByTestId("panel")[0]);
    expect(props.onDragOverChange).not.toHaveBeenCalled();
    leave(document.body);
    expect(props.onDragOverChange).toHaveBeenCalledWith(null);
  });

  it("drops with and without a dragging panel", () => {
    const props = makeProps({ draggingId: "one" });
    const { container, rerender } = render(
      <LiveTelemetryWorkspace {...props} />,
    );
    fireEvent.drop(items(container)[1]);
    expect(props.onMove).toHaveBeenCalledWith("one", "two");
    expect(props.onDraggingChange).toHaveBeenCalledWith(null);
    const props2 = makeProps();
    rerender(<LiveTelemetryWorkspace {...props2} />);
    fireEvent.drop(items(container)[1]);
    expect(props2.onMove).not.toHaveBeenCalled();
    expect(props2.onDragOverChange).toHaveBeenCalledWith(null);
  });

  it("forwards drag handle and size callbacks", () => {
    const props = makeProps();
    render(<LiveTelemetryWorkspace {...props} />);
    const handle = screen.getAllByTestId("drag-handle")[0];
    const setData = vi.fn();
    const ev = createEvent.dragStart(handle);
    Object.defineProperty(ev, "dataTransfer", {
      value: { effectAllowed: "", setData },
    });
    fireEvent(handle, ev);
    expect(props.onDraggingChange).toHaveBeenCalledWith("one");
    expect(setData).toHaveBeenCalledWith("text/plain", "one");
    fireEvent.dragEnd(handle);
    expect(props.onDraggingChange).toHaveBeenCalledWith(null);
    expect(props.onDragOverChange).toHaveBeenCalledWith(null);
    fireEvent.click(screen.getAllByText("resize")[1]);
    expect(props.onSizeChange).toHaveBeenCalledWith("two", {
      width: 10,
      height: 20,
    });
  });
});
