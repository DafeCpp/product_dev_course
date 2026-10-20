import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import type { ComponentProps } from "react";
import { describe, expect, it, vi } from "vitest";
import TelemetryHistoryPanel from "./TelemetryHistoryPanel";
import type { CaptureSession, Sensor } from "../../types";

vi.mock("./TelemetryHistorySettings", () => ({
  default: () => <div>SettingsMock</div>,
}));
vi.mock("./HistorySensorControls", () => ({
  default: ({
    sensors,
    limitReached,
  }: {
    sensors: Sensor[];
    limitReached: boolean;
  }) => (
    <div data-testid="controls">
      {`sensors:${sensors.length} limit:${String(limitReached)}`}
    </div>
  ),
}));
vi.mock("./TelemetryHistoryChart", () => ({
  default: ({
    onTimeRangeChange,
  }: {
    onTimeRangeChange: (range: [string, string] | null) => void;
  }) => (
    <button onClick={() => onTimeRangeChange(["a", "b"])}>ChartMock</button>
  ),
}));
vi.mock("../CaptureSessionTimeline", () => ({
  default: ({
    timeRange,
    onSessionClick,
  }: {
    timeRange: [string, string] | null;
    onSessionClick: (id: string) => void;
  }) => (
    <button onClick={() => onSessionClick("s9")}>
      {`Timeline:${timeRange ? timeRange.join("-") : "none"}`}
    </button>
  ),
}));

type Props = ComponentProps<typeof TelemetryHistoryPanel>;
type History = Props["history"];

function makeHistory(overrides: Partial<History> = {}): History {
  return {
    captureSessionId: "cs1",
    setCaptureSessionId: vi.fn(),
    sensorIds: [],
    valueMode: "physical",
    setValueMode: vi.fn(),
    includeLate: true,
    setIncludeLate: vi.fn(),
    maxPoints: 5000,
    setMaxPoints: vi.fn(),
    order: "asc",
    setOrder: vi.fn(),
    useAggregated: false,
    setUseAggregated: vi.fn(),
    error: null,
    filter: "",
    setFilter: vi.fn(),
    availableSensors: [],
    filteredSensors: [],
    sensorById: new Map(),
    rawSeries: [],
    aggregatedSeries: [],
    effectiveSensorIds: [],
    lastTimestamp: null,
    cursorBySensor: {},
    displayMaxPoints: 5000,
    loading: false,
    loadedCount: 0,
    loadError: null,
    wasTruncated: false,
    hasData: false,
    add: vi.fn(),
    addAll: vi.fn(),
    remove: vi.fn(),
    clear: vi.fn(),
    load: vi.fn(),
    ...overrides,
  } as unknown as History;
}

function renderPanel(overrides: Partial<Props> = {}) {
  const props: Props = {
    history: makeHistory(),
    sessions: [],
    sensors: [],
    runId: "r1",
    onContinueLive: vi.fn(),
    onExport: vi.fn(),
    ...overrides,
  };
  render(<TelemetryHistoryPanel {...props} />);
  return props;
}

const startedAt = "2024-01-01T10:00:00Z";
const stoppedAt = "2024-01-01T11:00:00Z";

describe("TelemetryHistoryPanel", () => {
  it("renders default title and disabled states without session", () => {
    renderPanel({
      history: makeHistory({ captureSessionId: "" }),
      runId: "",
    });
    expect(screen.getByText("Историческая выборка")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /Загрузить/ })).toBeDisabled();
    expect(
      screen.getByRole("button", { name: /Продолжить в live/ }),
    ).toBeDisabled();
    expect(screen.getByRole("button", { name: /Экспорт/ })).toBeDisabled();
    expect(screen.getByTestId("controls")).toHaveTextContent(
      "sensors:0 limit:false",
    );
    expect(screen.queryByText(/Timeline/)).not.toBeInTheDocument();
    expect(screen.queryByText(/Загружено/)).not.toBeInTheDocument();
  });

  it("renders session header with start and stop and enabled actions", async () => {
    const user = userEvent.setup();
    const history = makeHistory({
      effectiveSensorIds: ["a"],
      lastTimestamp: "2024-01-01T10:30:00Z",
      sensorById: new Map([["a", { id: "a", name: "A" } as Sensor]]),
      sensorIds: Array.from({ length: 50 }, (_, i) => `id${i}`),
    } as Partial<History>);
    const props = renderPanel({
      history,
      selectedSession: {
        id: "s1",
        ordinal_number: 3,
        started_at: startedAt,
        stopped_at: stoppedAt,
      } as CaptureSession,
    });
    expect(screen.getByText("Capture #3")).toBeInTheDocument();
    const start = new Date(startedAt).toLocaleString("ru-RU");
    const stop = new Date(stoppedAt).toLocaleString("ru-RU");
    expect(screen.getByText(`${start} → ${stop}`)).toBeInTheDocument();
    expect(screen.getByTestId("controls")).toHaveTextContent(
      "sensors:1 limit:true",
    );
    await user.click(screen.getByRole("button", { name: /Загрузить/ }));
    expect(history.load).toHaveBeenCalled();
    await user.click(screen.getByRole("button", { name: /Продолжить в live/ }));
    expect(props.onContinueLive).toHaveBeenCalled();
    await user.click(screen.getByRole("button", { name: /Экспорт/ }));
    expect(props.onExport).toHaveBeenCalled();
  });

  it("renders session without stop time and without start time", () => {
    renderPanel({
      selectedSession: {
        id: "s1",
        ordinal_number: 4,
        started_at: startedAt,
        stopped_at: null,
      } as CaptureSession,
    });
    expect(
      screen.getByText(new Date(startedAt).toLocaleString("ru-RU")),
    ).toBeInTheDocument();
  });

  it("hides sub header when session has no start", () => {
    const { container } = render(
      <TelemetryHistoryPanel
        history={makeHistory()}
        sessions={[]}
        sensors={[]}
        runId="r1"
        onContinueLive={vi.fn()}
        onExport={vi.fn()}
        selectedSession={
          { id: "s1", ordinal_number: 5, started_at: null } as CaptureSession
        }
      />,
    );
    expect(screen.getByText("Capture #5")).toBeInTheDocument();
    expect(
      container.querySelector(".history-panel-header__sub"),
    ).not.toBeInTheDocument();
  });

  it("shows loading label and history error", () => {
    renderPanel({
      history: makeHistory({ loading: true, error: "hist-err" }),
    });
    expect(screen.getByRole("button", { name: /Загрузка…/ })).toBeDisabled();
    expect(screen.getByText("hist-err")).toBeInTheDocument();
  });

  it("shows load error when no history error", () => {
    renderPanel({ history: makeHistory({ loadError: "load-err" }) });
    expect(screen.getByText("load-err")).toBeInTheDocument();
  });

  it("shows aggregated summary", () => {
    renderPanel({
      history: makeHistory({ loadedCount: 7, useAggregated: true }),
    });
    expect(screen.getByText("Загружено бакетов (1m): 7")).toBeInTheDocument();
  });

  it("shows truncated raw summary in desc and asc order", () => {
    renderPanel({
      history: makeHistory({
        loadedCount: 10,
        wasTruncated: true,
        order: "desc",
        displayMaxPoints: 10,
      }),
    });
    expect(
      screen.getByText("Загружено точек: 10 (показаны последние 10)"),
    ).toBeInTheDocument();
  });

  it("shows truncated summary in asc order with zero count", () => {
    renderPanel({
      history: makeHistory({ wasTruncated: true, displayMaxPoints: 3 }),
    });
    expect(
      screen.getByText("Загружено точек: 0 (показаны первые 3)"),
    ).toBeInTheDocument();
  });

  it("passes chart time range to timeline and handles session clicks", async () => {
    const user = userEvent.setup();
    const history = makeHistory();
    renderPanel({
      history,
      sessions: [{ id: "s9", ordinal_number: 1 } as CaptureSession],
    });
    expect(screen.getByText("Timeline:none")).toBeInTheDocument();
    await user.click(screen.getByText("ChartMock"));
    expect(screen.getByText("Timeline:a-b")).toBeInTheDocument();
    await user.click(screen.getByText("Timeline:a-b"));
    expect(history.setCaptureSessionId).toHaveBeenCalledWith("s9");
  });
});
