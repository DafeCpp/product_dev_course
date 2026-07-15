import { MaterialSelect } from "../common";
import type { Sensor } from "../../types";

interface HistorySensorControlsProps {
  sensors: Sensor[];
  selectedIds: string[];
  filteredSensors: Sensor[];
  filter: string;
  limitReached: boolean;
  onFilterChange: (value: string) => void;
  onAdd: (id: string) => void;
  onAddAll: () => void;
  onClear: () => void;
  onRemove: (id: string) => void;
}

export default function HistorySensorControls({
  sensors,
  selectedIds,
  filteredSensors,
  filter,
  limitReached,
  onFilterChange,
  onAdd,
  onAddAll,
  onClear,
  onRemove,
}: HistorySensorControlsProps) {
  return (
    <div className="telemetry-view__history-controls">
      <div className="telemetry-view__history-sensors">
        <div className="telemetry-view__sensor-filter">
          <label htmlFor="telemetry_history_sensor_filter">
            Фильтр сенсоров
          </label>
          <input
            id="telemetry_history_sensor_filter"
            type="text"
            className="telemetry-view__text-input"
            value={filter}
            onChange={(event) => onFilterChange(event.target.value)}
            placeholder="Имя, тип, id"
          />
        </div>
        <MaterialSelect
          id="telemetry_history_sensors"
          value=""
          label="Сенсоры"
          onChange={(value, event) => {
            onAdd(value);
            if (event?.currentTarget) event.currentTarget.value = "";
          }}
          disabled={!filteredSensors.length || limitReached}
        >
          <option value="">Добавить сенсор</option>
          {filteredSensors.map((sensor) => (
            <option key={sensor.id} value={sensor.id}>
              {sensor.name} ({sensor.type})
            </option>
          ))}
        </MaterialSelect>
        <div className="telemetry-view__sensor-actions">
          <button
            type="button"
            className="btn btn-secondary btn-xs"
            onClick={onAddAll}
            disabled={limitReached || !filteredSensors.length}
          >
            Добавить все
          </button>
          <button
            type="button"
            className="btn btn-ghost btn-xs"
            onClick={onClear}
            disabled={!selectedIds.length}
          >
            Очистить
          </button>
        </div>
        <div className="telemetry-view__sensor-meta">
          Выбрано {selectedIds.length} / 50
        </div>
        <div className="telemetry-view__sensor-list">
          {selectedIds.length === 0 && (
            <span className="telemetry-view__hint">Сенсоры не выбраны</span>
          )}
          {selectedIds.map((id) => (
            <span key={id} className="telemetry-view__sensor-pill">
              {sensors.find((sensor) => sensor.id === id)?.name || id}
              <button
                type="button"
                onClick={() => onRemove(id)}
                aria-label="Удалить сенсор"
              >
                ×
              </button>
            </span>
          ))}
        </div>
      </div>
    </div>
  );
}
