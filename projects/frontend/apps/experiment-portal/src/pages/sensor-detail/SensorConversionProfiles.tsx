import { format } from 'date-fns'
import type { ConversionProfile, ConversionProfileStatus, ConversionProfilesListResponse } from '../../types'
import { Loading } from '../../components/common'

interface ConversionProfileMutation {
  mutate: (profileId: string) => void
  isPending: boolean
}

interface SensorConversionProfilesProps {
  profilesData: ConversionProfilesListResponse | undefined
  isLoadingProfiles: boolean
  publishProfileMutation: ConversionProfileMutation
  onCreateProfile: () => void
}

const profileStatusLabels: Record<ConversionProfileStatus, string> = {
  draft: 'Черновик',
  scheduled: 'Запланирован',
  active: 'Активен',
  deprecated: 'Устаревший',
}

const profileStatusColors: Record<ConversionProfileStatus, string> = {
  draft: 'badge-secondary',
  scheduled: 'badge-info',
  active: 'badge-success',
  deprecated: 'badge-muted',
}

function formatProfileKind(kind: string): string {
  const labels: Record<string, string> = {
    linear: 'Линейное',
    polynomial: 'Полиномиальное',
    lookup_table: 'Таблица',
  }
  return labels[kind] || kind
}

export default function SensorConversionProfiles({
  profilesData,
  isLoadingProfiles,
  publishProfileMutation,
  onCreateProfile,
}: SensorConversionProfilesProps) {
  return (
    <div className="sensor-profiles-section">
      <div className="section-header">
        <h3>Профили преобразования</h3>
        <button
          className="btn btn-primary btn-sm"
          onClick={onCreateProfile}
        >
          Создать профиль
        </button>
      </div>

      {isLoadingProfiles && <Loading />}

      {!isLoadingProfiles && profilesData && (
        <>
          {profilesData.conversion_profiles.length === 0 ? (
            <p className="text-muted">Нет профилей преобразования</p>
          ) : (
            <div className="profiles-list">
              <table>
                <thead>
                  <tr>
                    <th>Версия</th>
                    <th>Тип</th>
                    <th>Статус</th>
                    <th>Создан</th>
                    <th>Действия</th>
                  </tr>
                </thead>
                <tbody>
                  {profilesData.conversion_profiles.map((profile: ConversionProfile) => (
                    <tr key={profile.id} className={profile.status === 'active' ? 'row-active' : ''}>
                      <td>
                        <strong>{profile.version}</strong>
                      </td>
                      <td>{formatProfileKind(profile.kind)}</td>
                      <td>
                        <span className={`badge ${profileStatusColors[profile.status]}`}>
                          {profileStatusLabels[profile.status]}
                        </span>
                      </td>
                      <td>
                        {format(new Date(profile.created_at), 'dd MMM yyyy HH:mm')}
                      </td>
                      <td>
                        {(profile.status === 'draft' || profile.status === 'scheduled') && (
                          <button
                            className="btn btn-primary btn-sm"
                            onClick={() => {
                              if (confirm('Опубликовать профиль? Текущий активный профиль будет деактивирован.')) {
                                publishProfileMutation.mutate(profile.id)
                              }
                            }}
                            disabled={publishProfileMutation.isPending}
                          >
                            {publishProfileMutation.isPending ? 'Публикация...' : 'Опубликовать'}
                          </button>
                        )}
                        {profile.status === 'active' && (
                          <span className="text-muted">Активный профиль</span>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </>
      )}
    </div>
  )
}
