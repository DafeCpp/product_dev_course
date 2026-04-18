import { lazy, Suspense } from 'react'
import { Routes, Route, Navigate } from 'react-router-dom'
import Layout from './components/Layout'
import ProtectedRoute from './components/ProtectedRoute'
import './App.scss'

const Login = lazy(() => import('./pages/Login'))
const Register = lazy(() => import('./pages/Register'))
const ChangePassword = lazy(() => import('./pages/ChangePassword'))
const ForgotPassword = lazy(() => import('./pages/ForgotPassword'))
const ResetPassword = lazy(() => import('./pages/ResetPassword'))
const ExperimentsList = lazy(() => import('./pages/ExperimentsList'))
const ExperimentDetail = lazy(() => import('./pages/ExperimentDetail'))
const RunDetail = lazy(() => import('./pages/RunDetail'))
const SensorsList = lazy(() => import('./pages/SensorsList'))
const CreateSensor = lazy(() => import('./pages/CreateSensor'))
const SensorDetail = lazy(() => import('./pages/SensorDetail'))
const ProjectsList = lazy(() => import('./pages/ProjectsList'))
const TelemetryViewer = lazy(() => import('./pages/TelemetryViewer'))
const Webhooks = lazy(() => import('./pages/Webhooks'))
const AdminUsers = lazy(() => import('./pages/AdminUsers'))
const SystemRoles = lazy(() => import('./pages/SystemRoles'))
const AuditLog = lazy(() => import('./pages/AuditLog'))
const Scripts = lazy(() => import('./pages/Scripts'))
const SensorMonitor = lazy(() => import('./pages/SensorMonitor'))
const ComparisonPage = lazy(() => import('./pages/ComparisonPage'))

function PageLoader() {
  return <div className="page-loader" aria-label="Загрузка страницы..." />
}

function App() {
  return (
    <Suspense fallback={<PageLoader />}>
      <Routes>
        <Route path="/login" element={<Login />} />
        <Route path="/register" element={<Register />} />
        <Route path="/forgot-password" element={<ForgotPassword />} />
        <Route path="/reset-password" element={<ResetPassword />} />
        <Route
          path="/change-password"
          element={
            <ProtectedRoute requirePasswordChanged={false}>
              <ChangePassword />
            </ProtectedRoute>
          }
        />
        <Route
          path="/*"
          element={
            <ProtectedRoute>
              <Layout>
                <Routes>
                  <Route path="/" element={<Navigate to="/experiments" replace />} />
                  <Route path="/projects" element={<ProjectsList />} />
                  <Route path="/experiments" element={<ExperimentsList />} />
                  <Route path="/experiments/:id" element={<ExperimentDetail />} />
                  <Route path="/experiments/:experimentId/compare" element={<ComparisonPage />} />
                  <Route path="/runs/:id" element={<RunDetail />} />
                  <Route path="/sensors" element={<SensorsList />} />
                  <Route path="/sensors/new" element={<CreateSensor />} />
                  <Route path="/sensors/:id" element={<SensorDetail />} />
                  <Route path="/sensor-monitor" element={<SensorMonitor />} />
                  <Route path="/telemetry" element={<TelemetryViewer />} />
                  <Route path="/webhooks" element={<Webhooks />} />
                  <Route path="/admin/users" element={<AdminUsers />} />
                  <Route path="/admin/system-roles" element={<SystemRoles />} />
                  <Route path="/admin/audit" element={<AuditLog />} />
                  <Route path="/admin/scripts" element={<Scripts />} />
                </Routes>
              </Layout>
            </ProtectedRoute>
          }
        />
      </Routes>
    </Suspense>
  )
}

export default App
