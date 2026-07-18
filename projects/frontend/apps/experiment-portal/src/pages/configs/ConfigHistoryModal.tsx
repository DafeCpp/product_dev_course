import { useQuery } from '@tanstack/react-query'
import { configsApi } from '../../api/configs'
import type { ConfigResponse } from '../../types/configs'
import Modal from '../../components/Modal'
import { Loading, Error as ErrorComponent, EmptyState } from '../../components/common'

export interface ConfigHistoryModalProps {
  config: ConfigResponse
  onClose: () => void
}

export default function ConfigHistoryModal({ config, onClose }: ConfigHistoryModalProps) {
  const { data, isLoading, error } = useQuery({
    queryKey: ['config-history', config.id],
    queryFn: () => configsApi.getHistory(config.id),
    staleTime: 10_000,
  })

  const items = data?.items ?? []

  return (
    <Modal isOpen onClose={onClose} title={`История «${config.key}»`} className="configs-history-modal">
      {isLoading && <Loading message="Загрузка истории..." />}
      {error && (
        <ErrorComponent message={error instanceof Error ? error.message : 'Ошибка загрузки истории'} />
      )}
      {!isLoading && !error && items.length === 0 && <EmptyState message="История пуста" />}
      {!isLoading && !error && items.length > 0 && (
        <ul className="configs-history">
          {items.map((h) => (
            <li key={h.id} className="configs-history__item">
              <div className="configs-history__head">
                <span className="configs-history__version">v{h.version}</span>
                <span className="configs-history__meta">
                  {h.changed_by} · {new Date(h.changed_at).toLocaleString()}
                  {h.is_active ? ' · active' : ''}
                </span>
              </div>
              {h.change_reason && <div className="configs-history__reason">{h.change_reason}</div>}
              <pre className="configs-history__value">{JSON.stringify(h.value, null, 2)}</pre>
            </li>
          ))}
        </ul>
      )}
    </Modal>
  )
}
