import { Routes, Route, Navigate } from 'react-router-dom'
import Layout from './components/Layout'
import ProtectedRoute from './components/ProtectedRoute'
import Login from './pages/Login'
import Register from './pages/Register'
import ChangePassword from './pages/ChangePassword'
import ExperimentsList from './pages/ExperimentsList'
import ExperimentDetail from './pages/ExperimentDetail'
import RunDetail from './pages/RunDetail'
import SensorsList from './pages/SensorsList'
import CreateSensor from './pages/CreateSensor'
import SensorDetail from './pages/SensorDetail'
import ProjectsList from './pages/ProjectsList'
import TelemetryViewer from './pages/TelemetryViewer'
import Webhooks from './pages/Webhooks'
import AdminUsers from './pages/AdminUsers'
import SensorMonitor from './pages/SensorMonitor'
import ComparisonPage from './pages/ComparisonPage'
import './App.scss'

function App() {
  return (
    <Routes>
      <Route path="/login" element={<Login />} />
      <Route path="/register" element={<Register />} />
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
              </Routes>
            </Layout>
          </ProtectedRoute>
        }
      />
    </Routes>
  )
}

export default App

